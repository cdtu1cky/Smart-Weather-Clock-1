#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "stm32f10x.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "esp_usart.h"


// 接收队列长度（字节）
#define RX_QUEUE_LENGTH     256

static QueueHandle_t xRxQueue;            // 接收队列，1 项 1 字节
static volatile uint32_t rx_overflow;     // 队列满丢字节的次数


void esp_usart_init(void)
{
    xRxQueue = xQueueCreate(RX_QUEUE_LENGTH, sizeof(uint8_t));

    GPIO_InitTypeDef GPIO_InitStructure;

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
    // 中断优先级要 >= configMAX_SYSCALL_INTERRUPT_PRIORITY，才能在中断里调用 FreeRTOS API
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 5;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    USART_InitTypeDef USART_InitStructure;
    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;

    USART_Init(USART2, &USART_InitStructure);
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
    USART_Cmd(USART2, ENABLE);
}

void esp_usart_write_data(uint8_t *data, uint16_t length)
{
    for (uint16_t i = 0; i < length; i++)
    {
        while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
        USART_SendData(USART2, data[i]);
    }
}

void esp_usart_write_string(const char *str)
{
    uint16_t len = strlen(str);
    esp_usart_write_data((uint8_t *)str, len);
}

QueueHandle_t esp_usart_get_rx_queue(void)
{
    return xRxQueue;
}

// 串口中断：把收到的字节放进队列
void USART2_IRQHandler(void)
{
    if(USART_GetITStatus(USART2, USART_IT_RXNE) == SET)
    {
        uint8_t data = USART_ReceiveData(USART2);
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;

        if (xQueueSendFromISR(xRxQueue, &data, &xHigherPriorityTaskWoken) != pdPASS)
        {
            rx_overflow++;
        }

        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}
