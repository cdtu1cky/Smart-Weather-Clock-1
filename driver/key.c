#include <stdbool.h>
#include <stdint.h>
#include "stm32f10x.h"
#include "FreeRTOS.h"
#include "task.h"
#include "key.h"


/* 按键引脚：默认 PA0
 * 如果你的板子按键接的是别的引脚，改下面两行即可 */
#define KEY_PORT    GPIOA
#define KEY_PIN     GPIO_Pin_0

/* 消抖时间(ms) */
#define KEY_DEBOUNCE_MS     20


void key_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    GPIO_InitStructure.GPIO_Pin = KEY_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;      // 上拉输入，按下接地
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(KEY_PORT, &GPIO_InitStructure);
}

/* 扫描按键，返回 true 表示检测到一次按下
 * 思路：只有电平变化并稳定超过消抖时间才认可，按住不放只算一次 */
bool key_scan(void)
{
    static uint8_t last_level = 1;      // 上拉，没按下时是高电平
    static TickType_t last_tick = 0;

    uint8_t level = GPIO_ReadInputDataBit(KEY_PORT, KEY_PIN);

    if (level != last_level)
    {
        if ((xTaskGetTickCount() - last_tick) >= pdMS_TO_TICKS(KEY_DEBOUNCE_MS))
        {
            last_tick = xTaskGetTickCount();
            last_level = level;

            if (level == 0)             // 按下
            {
                return true;
            }
        }
    }

    return false;
}
