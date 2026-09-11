#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H


/* FreeRTOS 配置：STM32F103C8 / Cortex-M3 / Keil */

/* 调度器 */
#define configUSE_PREEMPTION                     1
#define configUSE_TIME_SLICING                   1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION  0
#define configUSE_TICKLESS_IDLE                  0
#define configCPU_CLOCK_HZ                       ( 72000000UL )
#define configTICK_RATE_HZ                       ( 1000 )       /* 1ms 一个节拍 */
#define configMAX_PRIORITIES                     ( 7 )
#define configMINIMAL_STACK_SIZE                 ( 128 )        /* 单位：字 */
#define configMAX_TASK_NAME_LEN                  ( 16 )
#define configUSE_16_BIT_TICKS                   0
#define configIDLE_SHOULD_YIELD                  1

/* 内存管理 heap_4 */
#define configSUPPORT_STATIC_ALLOCATION          0
#define configSUPPORT_DYNAMIC_ALLOCATION         1
#define configTOTAL_HEAP_SIZE                    ( 6 * 1024 )   /* F103C8 只有 20KB RAM，先给 6KB */

/* 同步机制 */
#define configUSE_MUTEXES                        1
#define configUSE_RECURSIVE_MUTEXES              0
#define configUSE_COUNTING_SEMAPHORES            1
#define configUSE_QUEUE_SETS                     0
#define configUSE_TASK_NOTIFICATIONS             1
#define configQUEUE_REGISTRY_SIZE                0

/* 软件定时器（暂不用） */
#define configUSE_TIMERS                         0
#define configTIMER_TASK_PRIORITY                ( configMAX_PRIORITIES - 1 )
#define configTIMER_QUEUE_LENGTH                 10
#define configTIMER_TASK_STACK_DEPTH             ( configMINIMAL_STACK_SIZE * 2 )

/* 用到的 API（默认都是 0，用到哪个开哪个） */
#define INCLUDE_vTaskDelay                       1
#define INCLUDE_vTaskDelayUntil                  1
#define INCLUDE_xTaskGetSchedulerState           1
#define INCLUDE_xTaskGetCurrentTaskHandle        1
#define INCLUDE_uxTaskGetStackHighWaterMark      1
#define INCLUDE_vTaskDelete                      1

/* 协程（croutine.c 需要） */
#define configUSE_CO_ROUTINES                    0
#define configMAX_CO_ROUTINE_PRIORITIES          ( 2 )

/* 钩子函数 */
#define configUSE_IDLE_HOOK                      0
#define configUSE_TICK_HOOK                      0
#define configUSE_MALLOC_FAILED_HOOK             0
#define configCHECK_FOR_STACK_OVERFLOW           0
#define configUSE_DAEMON_TASK_STARTUP_HOOK       0

/* 统计 */
#define configUSE_TRACE_FACILITY                 0
#define configGENERATE_RUN_TIME_STATS            0
#define configUSE_STATS_FORMATTING_FUNCTIONS     0
#define configUSE_NEWLIB_REENTRANT               0

/* Cortex-M3 中断优先级（4 位） */
#define configPRIO_BITS                              4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY      15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5

#define configKERNEL_INTERRUPT_PRIORITY \
    ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )

#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )

/* 异常处理函数映射到标准名字 */
#define vPortSVCHandler          SVC_Handler
#define xPortPendSVHandler       PendSV_Handler
#define xPortSysTickHandler      SysTick_Handler

/* 断言 */
#define configASSERT( x )    if( ( x ) == 0 ) { taskDISABLE_INTERRUPTS(); for( ;; ); }


#endif /* FREERTOS_CONFIG_H */
