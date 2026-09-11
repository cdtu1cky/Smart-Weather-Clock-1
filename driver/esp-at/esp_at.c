#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "esp_usart.h"
#include "esp_at.h"


#define RX_BUFFER_SIZE  4096


static uint8_t rxdata[RX_BUFFER_SIZE];   // 本条 AT 回包
static uint32_t rxlen;                   // 回包长度
static QueueHandle_t xUartQueue;         // 串口接收队列


bool esp_at_init(void)
{
    esp_usart_init();

    xUartQueue = esp_usart_get_rx_queue();
    if (xUartQueue == NULL)
    {
        return false;
    }

    return esp_at_reset();
}

bool esp_at_send_command(const char *cmd, const char **rsp, uint32_t *length, uint32_t timeout)
{
    uint8_t data;
    bool result = false;
    TickType_t deadline;

    xQueueReset(xUartQueue);        // 清掉残留数据

    rxlen = 0;

    esp_usart_write_string(cmd);
    esp_usart_write_string("\r\n");

    deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout);

    for (;;)
    {
        TickType_t remain = deadline - xTaskGetTickCount();

        if ((int32_t)remain <= 0)
        {
            break;                  // 超时
        }

        // 阻塞等一个字节
        if (xQueueReceive(xUartQueue, &data, remain) != pdPASS)
        {
            break;
        }

        if (rxlen < RX_BUFFER_SIZE)
        {
            rxdata[rxlen++] = data;
        }

        // 判断行尾是不是 OK / ERROR
        if (data == '\n' && rxlen >= 2 && rxdata[rxlen - 2] == '\r')
        {
            if (rxlen >= 4 && rxdata[rxlen - 4] == 'O' && rxdata[rxlen - 3] == 'K')
            {
                result = true;
                break;
            }

            if (rxlen >= 7 && rxdata[rxlen - 7] == 'E' && rxdata[rxlen - 6] == 'R' &&
                rxdata[rxlen - 5] == 'R' && rxdata[rxlen - 4] == 'O' && rxdata[rxlen - 3] == 'R')
            {
                result = false;
                break;
            }
        }
    }

    if (rsp)
    {
        *rsp = (const char *)rxdata;
    }
    if (length)
    {
        *length = rxlen;
    }

    return result;
}

bool esp_at_send_data(const uint8_t *data, uint32_t length)
{
    esp_usart_write_data((uint8_t *)data, length);

    return true;
}

bool esp_at_reset(void)
{
    // 复位esp32
    if (!esp_at_send_command("AT+RESTORE", NULL, NULL, 1000))
    {
        return false;
    }
    delay_ms(2000);
    // 关闭回显
    if (!esp_at_send_command("ATE0", NULL, NULL, 1000))
    {
        return false;
    }
    // 关闭存储
    if (!esp_at_send_command("AT+SYSSTORE=0", NULL, NULL, 1000))
    {
        return false;
    }

    return true;
}

bool esp_at_wifi_init(void)
{
    // 设置为station模式
    if (!esp_at_send_command("AT+CWMODE=1", NULL, NULL, 1000))
    {
        return false;
    }

    return true;
}
bool esp_at_wifi_connect(const char *ssid, const char *pwd)
{
    char cmd[64];

    // 连接wifi
    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", ssid, pwd);
    if (!esp_at_send_command(cmd, NULL, NULL, 10000))
    {
        return false;
    }

    return true;
}

bool esp_at_get_http(const char *url, const char **rsp, uint32_t *length, uint32_t timeout)
{
    char cmd[128];

    snprintf(cmd, sizeof(cmd), "AT+HTTPCGET=\"%s\"", url);
    if (!esp_at_send_command(cmd, rsp, length, 10000))
    {
        return false;
    }

    return true;
}

bool esp_at_sntp_init(void)
{
    // 设置为SNTP模式
    if (!esp_at_send_command("AT+CIPSNTPCFG=1,8,\"cn.ntp.org.cn\",\"ntp.sjtu.edu.cn\"", NULL, NULL, 1000))
    {
        return false;
    }

    // 查询sntp时间
    if (!esp_at_send_command("AT+CIPSNTPTIME?", NULL, NULL, 1000))
    {
        return false;
    }

    return true;
}

bool esp_at_get_time(uint32_t *timestamp)
{
    const char *rsp;
    uint32_t length;

    if (!esp_at_send_command("AT+SYSTIMESTAMP?", &rsp, &length, 1000))
    {
        return false;
    }

    char *sts = strstr(rsp, "+SYSTIMESTAMP:") + strlen("+SYSTIMESTAMP:");

    *timestamp = atoi(sts);

    return true;
}

bool esp_at_wifi_get_ip(char ip[16])
{
    const char *rsp;
    if (!esp_at_send_command("AT+CIPSTA?", &rsp, NULL, 1000))
    {
        return false;
    }

    // 解析ip地址
    const char *pip = strstr(rsp, "+CIPSTA:ip:") + strlen("+CIPSTA:ip:");
    if (pip)
    {
        for (int i = 0; i < 16; i++)
        {
            if (pip[i] == '\r')
            {
                ip[i] = '\0';
                break;
            }
            ip[i] = pip[i];
        }
        return true;
    }

    return true;
}

bool esp_at_wifi_get_mac(char mac[18])
{
    const char *rsp;
    if (!esp_at_send_command("AT+CIPSTAMAC?", &rsp, NULL, 1000))
    {
        return false;
    }

    // 解析mac地址
    const char *pmac = strstr(rsp, "+CIPSTAMAC:mac:") + strlen("+CIPSTAMAC:mac:");
    if (pmac)
    {
        strncpy(mac, pmac, 18);
        return true;
    }

    return true;
}
