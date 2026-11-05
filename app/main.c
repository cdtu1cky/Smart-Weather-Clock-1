#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "stm32f10x.h"
#include "main.h"
#include "led.h"
#include "rtc.h"
#include "timer.h"
#include "esp_at.h"
#include "mpu6050.h"
#include "st7735.h"
#include "stfonts.h"
#include "stimage.h"
#include "weather.h"
#include "key.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"


static const char *wifi_ssid = "iq";
static const char *wifi_password = "1514324332";
static const char *city_name = "chengdu";
static const char *weather_uri = "https://api.seniverse.com/v3/weather/now.json?key=SiS93XYHj9_tQhARM&location=chengdu&language=en&unit=c";


#include "stm32f10x_iwdg.h"     // 看门狗

// RTOS 对象
static SemaphoreHandle_t xLcdMutex;      // 屏幕互斥量
static SemaphoreHandle_t xSysReadySem;   // 系统就绪信号量
static QueueHandle_t     xTemperQueue;   // 温度队列（长度1，只留最新值）
static QueueHandle_t     xWeatherQueue;  // 天气队列（长度1，只留最新值）

// 任务栈大小（单位：字，1 字 = 4 字节）
#define TASK_AT_STACK        320
#define TASK_DISPLAY_STACK   192
#define TASK_SENSOR_STACK    128
#define TASK_MONITOR_STACK   128

// 任务优先级
#define PRIO_AT        3
#define PRIO_DISPLAY   2
#define PRIO_SENSOR    1
#define PRIO_MONITOR   1

#define USE_WATCHDOG    1        // 调试时可改成 0 关闭看门狗

static volatile uint32_t hb_display;   // 显示任务心跳
static volatile uint32_t hb_sensor;    // 传感器任务心跳

// 看门狗初始化，LSI 40kHz 64 分频，超时约 4 秒
static void iwdg_init(void)
{
#if USE_WATCHDOG
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(IWDG_Prescaler_64);
    IWDG_SetReload(2500);
    IWDG_ReloadCounter();
    IWDG_Enable();
#endif
}

// 看门狗任务：显示和传感器任务的心跳都在动才喂狗
static void vTaskMonitor(void *pvParameters)
{
    (void)pvParameters;

    uint32_t last_display = 0;
    uint32_t last_sensor  = 0;
    uint32_t grace = 3;              // 启动宽限，前3秒无条件喂狗

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (grace > 0)
        {
            grace--;
            IWDG_ReloadCounter();
            continue;
        }

        if (hb_display != last_display && hb_sensor != last_sensor)
        {
            last_display = hb_display;
            last_sensor  = hb_sensor;
            IWDG_ReloadCounter();
        }
    }
}

static uint32_t disp_height;         // 开机信息显示位置

// 任务句柄（系统信息页要查询各�;9�务的栈余量）
static TaskHandle_t hTaskAT;
static TaskHandle_t hTaskDisplay;
static TaskHandle_t hTaskSensor;
static TaskHandle_t hTaskMonitor;

// 当前页面：0=时钟页 1=系统信息页
#define PAGE_CLOCK      0
#define PAGE_INFO       1
#define PAGE_COUNT      2
static uint8_t g_page;

// 画页面的固定内容（切页时调用一次）
static void page_draw_static(uint8_t page);

static void lcd_lock(void)   { xSemaphoreTake(xLcdMutex, portMAX_DELAY); }
static void lcd_unlock(void) { xSemaphoreGive(xLcdMutex); }

// 联网，返回 true 表示成功
static bool at_connect_all(void)
{
    lcd_lock();
    st7735_write_string(0, disp_height, "Init ESP32...", &font_ascii_8x16, ST7735_WHITE, ST7735_BLACK);
    lcd_unlock();
    disp_height += font_ascii_8x16.height;
    if (!esp_at_init())
    {
        return false;
    }

    lcd_lock();
    st7735_write_string(0, disp_height, "Init WIFI...", &font_ascii_8x16, ST7735_WHITE, ST7735_BLACK);
    lcd_unlock();
    disp_height += font_ascii_8x16.height;
    if (!esp_at_wifi_init())
    {
        return false;
    }

    lcd_lock();
    st7735_write_string(0, disp_height, "Connect WIFI...", &font_ascii_8x16, ST7735_WHITE, ST7735_BLACK);
    lcd_unlock();
    disp_height += font_ascii_8x16.height;
    if (!esp_at_wifi_connect(wifi_ssid, wifi_password))
    {
        return false;
    }

    lcd_lock();
    st7735_write_string(0, disp_height, "Sync Time...", &font_ascii_8x16, ST7735_WHITE, ST7735_BLACK);
    lcd_unlock();
    disp_height += font_ascii_8x16.height;
    if (!esp_at_sntp_init())
    {
        return false;
    }

    // 联网校时，+8 小时为北京时间
    uint32_t ts;
    if (esp_at_get_time(&ts))
    {
        rtc_set_timestamp(ts + 8 * 60 * 60);
    }

    return true;
}

static void vTaskAT(void *pvParameters)
{
    (void)pvParameters;

    // 联网失败就重试
    while (!at_connect_all())
    {
        lcd_lock();
        st7735_fill_screen(ST7735_BLACK);
        st7735_write_string(0, 0, "Connect failed, retry...", &font_ascii_8x16, ST7735_RED, ST7735_BLACK);
        lcd_unlock();

        disp_height = font_ascii_8x16.height;
        vTaskDelay(pdMS_TO_TICKS(5000));
    }

    lcd_lock();
    st7735_write_string(0, disp_height, "Ready", &font_ascii_8x16, ST7735_GREEN, ST7735_BLACK);
    lcd_unlock();

    xSemaphoreGive(xSysReadySem);        // 通知显示任务进入主界面

    // 周期维护天气和校时
    TickType_t now0 = xTaskGetTickCount();
    TickType_t next_weather = now0;                                 // 立即拉一次天气
    TickType_t next_sntp    = now0 + pdMS_TO_TICKS(60 * 60 * 1000); // 1 小时后再校时

    for (;;)
    {
        TickType_t now = xTaskGetTickCount();

        // 天气：成功 10 分钟后，失败 30 秒后
        if ((int32_t)(now - next_weather) >= 0)
        {
            const char *rsp = NULL;

            if (esp_at_get_http(weather_uri, &rsp, NULL, 10000) && rsp != NULL)
            {
                weather_t weather;
                if (weather_parse(rsp, &weather))
                {
                    xQueueOverwrite(xWeatherQueue, &weather);
                }
                next_weather = now + pdMS_TO_TICKS(10 * 60 * 1000);
            }
            else
            {
                next_weather = now + pdMS_TO_TICKS(30 * 1000);
            }
        }

        // 校时：成功 1 小时后，失败 30 秒后
        if ((int32_t)(now - next_sntp) >= 0)
        {
            uint32_t ts;
            if (esp_at_get_time(&ts))
            {
                rtc_set_timestamp(ts + 8 * 60 * 60);
                next_sntp = now + pdMS_TO_TICKS(60 * 60 * 1000);
            }
            else
            {
                next_sntp = now + pdMS_TO_TICKS(30 * 1000);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// 传感器任务：每秒读一次温度
static void vTaskSensor(void *pvParameters)
{
    (void)pvParameters;

    for (;;)
    {
        float temper = mpu6050_read_temper();
        xQueueOverwrite(xTemperQueue, &temper);

        hb_sensor++;

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// 画页面的固定内容
static void page_draw_static(uint8_t page)
{
    if (page == PAGE_CLOCK)
    {
        st7735_fill_screen(ST7735_BLACK);
        st7735_write_string(0, 112, "cky2304023133", &font_ascii_8x16, ST7735_WHITE, ST7735_BLACK);
        st7735_write_string(48, 48, city_name, &font_ascii_8x16, ST7735_YELLOW, ST7735_BLACK);
    }
    else
    {
        st7735_fill_screen(ST7735_BLACK);
        st7735_write_string(0, 0, "System Info", &font_ascii_8x16, ST7735_WHITE, ST7735_BLACK);
        st7735_write_string(0, 112, "cky2304023133", &font_ascii_8x16, ST7735_WHITE, ST7735_BLACK);
    }
}

// 时钟页刷新
static void page_refresh_clock(void)
{
    char str[64];

    // 日期和时间
    rtc_date_t date;
    rtc_get_date(&date);

    snprintf(str, sizeof(str), "%02d-%02d-%02d", date.year % 100, date.month, date.day);
    st7735_write_string(0, 0, str, &font_ascii_8x16, ST7735_WHITE, ST7735_BLACK);

    snprintf(str, sizeof(str), "%02d%s%02d", date.hour, date.second % 2 ? " " : ":", date.minute);
    st7735_write_string(0, 64, str, &font_time_24x48, ST7735_CYAN, ST7735_BLACK);

    // 有新温度就更新
    float temper;
    if (xQueueReceive(xTemperQueue, &temper, 0) == pdPASS)
    {
        snprintf(str, sizeof(str), "%4.1f", temper);
        st7735_write_string(78, 32, str, &font_ascii_8x16, ST7735_GREEN, ST7735_BLACK);
        st7735_draw_image(78 + (uint32_t)strlen(str) * 8, 32,
                          icon_wendu.width, icon_wendu.height, icon_wendu.data);
    }

    // 有新天气就更新
    weather_t weather;
    if (xQueueReceive(xWeatherQueue, &weather, 0) == pdPASS)
    {
        const st_image_t *img = NULL;
        if (strcmp(weather.weather, "Cloudy") == 0)        img = &icon_weather_duoyun;
        else if (strcmp(weather.weather, "Wind") == 0)     img = &icon_weather_feng;
        else if (strcmp(weather.weather, "Clear") == 0)    img = &icon_weather_qing;
        else if (strcmp(weather.weather, "Snow") == 0)     img = &icon_weather_xue;
        else if (strcmp(weather.weather, "Overcast") == 0) img = &icon_weather_yin;
        else if (strcmp(weather.weather, "Rain") == 0)     img = &icon_weather_yu;

        if (img != NULL)
        {
            st7735_draw_image(0, 16, img->width, img->height, img->data);
        }
        else
        {
            st7735_write_string(0, 16, weather.weather, &font_ascii_8x16, ST7735_YELLOW, ST7735_BLACK);
        }

        st7735_write_string(78, 0, weather.temperature, &font_temper_16x32, ST7735_BLUE, ST7735_BLACK);
        st7735_draw_image(78 + (uint32_t)strlen(weather.temperature) * 16, 8,
                          icon_wendu.width, icon_wendu.height, icon_wendu.data);
    }
}

// 系统信息页刷新（每秒刷一次就够）
static void page_refresh_info(void)
{
    static TickType_t last_tick = 0;
    char str[64];
    TickType_t now = xTaskGetTickCount();
    uint32_t sec;

    if ((now - last_tick) < pdMS_TO_TICKS(1000))
    {
        return;
    }
    last_tick = now;

    // 堆剩余
    snprintf(str, sizeof(str), "Heap %u", (unsigned)xPortGetFreeHeapSize());
    st7735_write_string(0, 16, str, &font_ascii_8x16, ST7735_CYAN, ST7735_BLACK);

    // 运行时长
    sec = now / configTICK_RATE_HZ;
    snprintf(str, sizeof(str), "Run %02u:%02u:%02u",
             (unsigned)(sec / 3600), (unsigned)((sec / 60) % 60), (unsigned)(sec % 60));
    st7735_write_string(0, 32, str, &font_ascii_8x16, ST7735_CYAN, ST7735_BLACK);

    // 各任务栈余量（单位：字）
    snprintf(str, sizeof(str), "AT   %u", (unsigned)uxTaskGetStackHighWaterMark(hTaskAT));
    st7735_write_string(0, 48, str, &font_ascii_8x16, ST7735_GREEN, ST7735_BLACK);

    snprintf(str, sizeof(str), "Disp %u", (unsigned)uxTaskGetStackHighWaterMark(hTaskDisplay));
    st7735_write_string(0, 64, str, &font_ascii_8x16, ST7735_GREEN, ST7735_BLACK);

    snprintf(str, sizeof(str), "Sens %u", (unsigned)uxTaskGetStackHighWaterMark(hTaskSensor));
    st7735_write_string(0, 80, str, &font_ascii_8x16, ST7735_GREEN, ST7735_BLACK);

    snprintf(str, sizeof(str), "Mon  %u", (unsigned)uxTaskGetStackHighWaterMark(hTaskMonitor));
    st7735_write_string(0, 96, str, &font_ascii_8x16, ST7735_GREEN, ST7735_BLACK);
}

// 显示任务：刷新屏幕 + 按键切页
static void vTaskDisplay(void *pvParameters)
{
    (void)pvParameters;
    TickType_t last_key_tick = 0;

    // 等联网完成再进主界面，等待期间也要喂心跳
    while (xSemaphoreTake(xSysReadySem, pdMS_TO_TICKS(100)) != pdPASS)
    {
        hb_display++;
    }

    lcd_lock();
    page_draw_static(g_page);
    lcd_unlock();

    TickType_t last_wake = xTaskGetTickCount();

    for (;;)
    {
        // 按键切页（再加 300ms 防连击）
        if (key_scan() && (xTaskGetTickCount() - last_key_tick) >= pdMS_TO_TICKS(300))
        {
            last_key_tick = xTaskGetTickCount();
            g_page = (g_page + 1) % PAGE_COUNT;

            lcd_lock();
            page_draw_static(g_page);
            lcd_unlock();
        }

        lcd_lock();
        if (g_page == PAGE_CLOCK)
        {
            page_refresh_clock();
        }
        else
        {
            page_refresh_info();
        }
        lcd_unlock();

        hb_display++;

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(100));
    }
}

int main(void)
{
    board_lowlevel_init();

    led_init();
    rtc_init();
    timer_init(1000);      // 1ms 硬件定时器
    timer_start();

    mpu6050_init();

    key_init();

    st7735_init();
    st7735_fill_screen(ST7735_BLACK);

    // 显示开机内容
    st7735_write_string(0, 0, "Initializing...", &font_ascii_8x16, ST7735_WHITE, ST7735_BLACK);
    disp_height += font_ascii_8x16.height;
    delay_ms(500);

    st7735_write_string(0, disp_height, "Wait ESP32...", &font_ascii_8x16, ST7735_WHITE, ST7735_BLACK);
    disp_height += font_ascii_8x16.height;
    delay_ms(1500);

    // 创建 RTOS 对象
    xLcdMutex     = xSemaphoreCreateMutex();
    xSysReadySem  = xSemaphoreCreateBinary();
    xTemperQueue  = xQueueCreate(1, sizeof(float));
    xWeatherQueue = xQueueCreate(1, sizeof(weather_t));

    if (xLcdMutex == NULL || xSysReadySem == NULL ||
        xTemperQueue == NULL || xWeatherQueue == NULL)
    {
        st7735_write_string(0, disp_height, "Create RTOS obj failed", &font_ascii_8x16, ST7735_RED, ST7735_BLACK);
        while (1);
    }

    // 创建任务（保存句柄，系统信息页要用）
    if (xTaskCreate(vTaskAT,      "AT",      TASK_AT_STACK,      NULL, PRIO_AT,      &hTaskAT) != pdPASS ||
        xTaskCreate(vTaskDisplay, "Display", TASK_DISPLAY_STACK, NULL, PRIO_DISPLAY, &hTaskDisplay) != pdPASS ||
        xTaskCreate(vTaskSensor,  "Sensor",  TASK_SENSOR_STACK,  NULL, PRIO_SENSOR,  &hTaskSensor) != pdPASS ||
        xTaskCreate(vTaskMonitor, "Monitor", TASK_MONITOR_STACK, NULL, PRIO_MONITOR, &hTaskMonitor) != pdPASS)
    {
        st7735_write_string(0, disp_height, "Create task failed", &font_ascii_8x16, ST7735_RED, ST7735_BLACK);
        while (1);
    }

    iwdg_init();

    // 启动调度器
    vTaskStartScheduler();

    // 只有调度器启动失败才会执行到这里
    st7735_write_string(0, disp_height, "Start scheduler failed", &font_ascii_8x16, ST7735_RED, ST7735_BLACK);
    while (1);
}
