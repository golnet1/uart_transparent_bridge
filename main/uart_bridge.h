#pragma once

#include "app.h"

/* target: 1 = UART1, 2 = UART2, 3 = оба, 0 = только USB (отладочный вывод) */
void uart_bridge_init(void);
bool uart_bridge_usb_ready(void);
void uart_bridge_apply_config(void);
int  uart_bridge_detected_baud(int ch);   /* 1 или 2; 0 = автоскорость не определена */
void uart_bridge_send(int target, const uint8_t *data, size_t len);
void uart_bridge_send_str(int target, const char *str);
void uart_bridge_usb_print(const char *str);
const char *uart_bridge_target_name(int target);
void uart_bridge_counters(uint32_t *rx1, uint32_t *rx2,
                          uint32_t *tx1, uint32_t *tx2);
