#pragma once

#include "app.h"

void  wifi_init(void);
void  wifi_reconnect(void);
bool  wifi_sta_connected(void);
bool  wifi_ap_enabled(void);
int   wifi_rssi(void);
const char *wifi_ip_ap(void);
const char *wifi_ip_sta(void);

typedef struct {
    char ssid[33];
    int  rssi;
    int  channel;
    bool auth;
} wifi_ap_t;

/* Блокирующий скан; возвращает количество найденных точек. */
int wifi_scan(wifi_ap_t *out, int max);
