#include <stdint.h>
#include "stm32f10x.h"
#include "FreeRTOS.h"
#include "task.h"


// DWT 周期计数器，用来做微秒延时
// （core_cm3.h 里没有定义 DWT，这里直接按寄存器地址访问）
#define DEMCR_REG            ( *( volatile uint32_t * ) 0xE000EDFCUL )
#define DEMCR_TRCENA         ( 1UL << 24 )
#define DWT_CTRL_REG         ( *( volatile uint32_t * ) 0xE0001000UL )
#define DWT_CYCCNT_REG       ( *( volatile uint32_t * ) 0xE0001004UL )
#define DWT_CTRL_CYCCNTENA   ( 1UL << 0 )

static void dwt_init(void)
{
    DEMCR_REG |= DEMCR_TRCENA;
    DWT_CYCCNT_REG = 0;
    DWT_CTRL_REG |= DWT_CTRL_CYCCNTENA;
}

// 注意：调度器启动后 SysTick 归 FreeRTOS 管，不能再去改它
void delay_us(uint32_t us)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED)
    {
        // 调度器还没启动，可以用 SysTick
        SysTick->LOAD = us * (SystemCoreClock / 1000000) - 1;
        SysTick->VAL = 0;
        SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
        while ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) == 0);
        SysTick->CTRL = 0;
    }
    else
    {
        // 调度器已启动，用 DWT 计数
        if ((DWT_CTRL_REG & DWT_CTRL_CYCCNTENA) == 0)
        {
            dwt_init();
        }

        uint32_t start = DWT_CYCCNT_REG;
        uint32_t ticks = us * (SystemCoreClock / 1000000UL);

        while ((DWT_CYCCNT_REG - start) < ticks);
    }
}

void delay_ms(uint32_t ms)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED)
    {
        while (ms--)
        {
            delay_us(1000);
        }
    }
    else
    {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
}
    }
}
