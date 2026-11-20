#pragma once

#include "app.h"

typedef struct {
    /* UART */
    int  baud1;
    int  baud2;
    bool bridge;          /* сквозная пересылка UART1 <-> UART2 */
    bool echo;            /* дублировать принятые байты обратно в источник */
    bool mirror;          /* зеркалить трафик на USB (UART0) */
    bool syslog;          /* выводить системные логи на USB */
    int  usb_target;      /* 1, 2 или 3 (оба) — куда идут команды из USB */

    /* автокоманда от самого ESP32 */
    int  auto_ms;         /* 0 = выключено */
    char auto_target[8];  /* "1", "2", "both", "usb" */
    char auto_text[80];

    /* сеть */
    char wifi_ssid[32];
    char wifi_pass[64];
    char ota_url[192];

    /* каналы команд */
    int  cmd_mode;            /* 0=адресная строка, 1=MQTT, 2=оба */
    int  cmd_target;          /* 1, 2 или 3 — цель по умолчанию */
    char mqtt_uri[160];       /* mqtt://host:1883 */
    char mqtt_topic[64];      /* базовый топик */
    char mqtt_user[48];
    char mqtt_pass[64];

    /* синхронизация времени */
    char ntp_server[96];
    char ntp_tz[48];          /* POSIX TZ, например MSK-3 или UTC0 */

    /* автоопределение скорости — новые поля в КОНЦЕ структуры для совместимости NVS */
    bool auto_baud1;
    bool auto_baud2;
    char baud_probe[64];      /* команды опроса через '|', например AT|AT+GMR */

    /* автообновление прошивки — новое поле в КОНЦЕ для совместимости NVS */
    bool auto_update;
} app_config_t;

const app_config_t *config_get(void);
app_config_t       *config_mut(void);   /* изменили -> вызвать config_save() */
void                config_load(void);
void                config_save(void);
void                config_reset(void);
