#include "time_sync.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_log.h"
#include "esp_sntp.h"

#include "config.h"

static const char *TAG = "time";
static bool s_synced;

static void sync_cb(struct timeval *tv)
{
    s_synced = true;   /* IDF сбрасывает sntp_get_sync_status() после колбэка */
    char buf[40];
    time_sync_format(buf, sizeof(buf));
    ESP_LOGI(TAG, "NTP синхронизировано: %s (tv_sec=%ld)", buf, (long)tv->tv_sec);
}

static void apply_tz(const char *tz)
{
    setenv("TZ", tz && tz[0] ? tz : "MSK-3", 1);
    tzset();
}

static void start(void)
{
    const app_config_t *c = config_get();
    apply_tz(c->ntp_tz);
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    sntp_set_time_sync_notification_cb(sync_cb);
    esp_sntp_setservername(0, c->ntp_server[0] ? c->ntp_server : "pool.ntp.org");
    esp_sntp_init();
    ESP_LOGI(TAG, "NTP сервер: %s, TZ: %s", c->ntp_server, c->ntp_tz);
}

void time_sync_init(void)
{
    start();
}

void time_sync_apply(void)
{
    esp_sntp_stop();
    esp_sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
    start();
}

bool time_sync_synced(void)
{
    return s_synced || sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED;
}

int64_t time_sync_epoch(void)
{
    return time_sync_synced() ? (int64_t)time(NULL) : 0;
}

int time_sync_offset_min(void)
{
    if (!time_sync_synced()) return 0;
    time_t now = time(NULL);
    struct tm ti;
    if (!localtime_r(&now, &ti)) return 0;
    char z[16] = "";
    if (!strftime(z, sizeof(z), "%z", &ti) || (z[0] != '+' && z[0] != '-')) return 0;
    int sign = (z[0] == '-') ? -1 : 1;
    int hh = (z[1] - '0') * 10 + (z[2] - '0');
    int mm = (z[3] - '0') * 10 + (z[4] - '0');
    return sign * (hh * 60 + mm);
}

void time_sync_format(char *buf, size_t cap)
{
    if (!buf || !cap) return;
    if (!time_sync_synced()) {
        snprintf(buf, cap, "--.--.---- --:--:--");
        return;
    }
    time_t now = time(NULL);
    struct tm ti;
    if (!localtime_r(&now, &ti)) {
        snprintf(buf, cap, "--.--.---- --:--:--");
        return;
    }
    snprintf(buf, cap, "%02d.%02d.%04d %02d:%02d:%02d",
             ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900,
             ti.tm_hour, ti.tm_min, ti.tm_sec);
}

void time_format_uptime(char *buf, size_t cap, uint32_t seconds)
{
    if (!buf || !cap) return;
    uint32_t d = seconds / 86400;
    uint32_t h = (seconds % 86400) / 3600;
    uint32_t m = (seconds % 3600) / 60;
    uint32_t s = seconds % 60;

    if (d)       snprintf(buf, cap, "%u д %02u ч %02u мин", (unsigned)d, (unsigned)h, (unsigned)m);
    else if (h)  snprintf(buf, cap, "%u ч %02u мин", (unsigned)h, (unsigned)m);
    else if (m)  snprintf(buf, cap, "%u мин %02u с", (unsigned)m, (unsigned)s);
    else         snprintf(buf, cap, "%u с", (unsigned)s);
}
