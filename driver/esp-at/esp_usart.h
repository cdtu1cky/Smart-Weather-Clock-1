#ifndef __ESP_USART_H
#define __ESP_USART_H


#include <stdint.h>
#include "FreeRTOS.h"
#include "queue.h"


void esp_usart_init(void);
void esp_usart_write_data(uint8_t *data, uint16_t length);
void esp_usart_write_string(const char *str);

// 串口接收队列（中断入队，任务出队）
QueueHandle_t esp_usart_get_rx_queue(void);


#endif /* __ESP_USART_H */
