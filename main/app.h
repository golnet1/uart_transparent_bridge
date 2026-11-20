#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define APP_NAME    "Pass-Through UART"
#define APP_SSID    "UART_BRIDGE"
#define APP_VERSION "1.0.6"

/* Типы сообщений в общей шине событий */
typedef enum {
    EV_FROM1   = 1,   /*_priнято с UART1 и передано на UART2 */
    EV_FROM2   = 2,   /* принято с UART2 и передано на UART1 */
    EV_FROMUSB = 3,   /* команда, набранная в USB-терминале */
    EV_TO1     = 4,   /* локальная отправка в UART1 */
    EV_TO2     = 5,   /* локальная отправка в UART2 */
    EV_SYS     = 6,   /* системные сообщения / логи */
    EV_OTA     = 7,
    EV_WIFI    = 8,
    EV_MAX
} ev_type_t;

#define SUB_ALL      ((1U << EV_MAX) - 1U)
#define SUB_TRAFFIC  ((1U << EV_FROM1) | (1U << EV_FROM2) | (1U << EV_FROMUSB) | \
                      (1U << EV_TO1) | (1U << EV_TO2))
#define SUB_SYS      ((1U << EV_SYS) | (1U << EV_OTA) | (1U << EV_WIFI))

/* Сообщение в очереди подписчика. Память освобождает ПОДПИСЧИК. */
typedef struct {
    uint32_t type;
    int64_t  ts_ms;
    char     line[];   /* гибкий массив: строка без завершающего \n */
} app_msg_t;

#define EV_BIT(t) (1U << (t))
