#include "telnet.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "app.h"
#include "cli.h"
#include "charset.h"
#include "auth.h"
#include "event_bus.h"

#define TELNET_PORT 23
#define MAX_CLIENTS 4
#define TEL_LINE_MAX 512

#define IAC   0xFF
#define DONT  0xFE
#define DO    0xFD
#define WONT  0xFC
#define WILL  0xFB
#define SB    0xFA     /* begin subnegotiation */
#define SE    0xF0     /* end subnegotiation */

#define OPT_ECHO 1
#define OPT_SGA  3

typedef struct {
    int   fd;
    bool  used;
    bool  monitor;
    bool  echo;           /* отвечать на ввод locally (по согласованию Telnet) */
    bool  auth_pending;   /* ждём пароль */
    bool  close_request;
    int64_t auth_deadline;
    char  pass[64];
    int   pass_len;
    char  line[TEL_LINE_MAX];
    int   len;
    int   iac_state;      /* 0 - нет, 1 - после IAC, 2 - ожидание опции */
    uint8_t iac_cmd;
    QueueHandle_t out_q;   /* char* */
    QueueHandle_t evt_q;   /* app_msg_t* */
    cli_session_t cli;
} telnet_client_t;

static const char *TAG = "telnet";
static telnet_client_t s_clients[MAX_CLIENTS];
static int s_listen = -1;

static void raw_send(telnet_client_t *c, const void *buf, size_t len)
{
    if (c->fd < 0) return;
    send(c->fd, buf, len, 0);
}

/* Строки прошивки в UTF-8. Терминал Windows ждёт CP866 — переводим. */
static void send_text(telnet_client_t *c, const char *text, bool crlf)
{
    if (!text) return;
    char *conv = charset_convert(text);
    const char *out = conv ? conv : text;
    raw_send(c, out, strlen(out));
    if (crlf) raw_send(c, "\r\n", 2);
    free(conv);
}

static void put_line(telnet_client_t *c, const char *line)
{
    char *s = strdup(line);
    if (!s) return;
    if (xQueueSend(c->out_q, &s, 0) != pdTRUE) free(s);
}

static void telnet_out(void *ctx, const char *line)
{
    put_line((telnet_client_t *)ctx, line);
}

static void drain_out(telnet_client_t *c)
{
    char *s;
    while (xQueueReceive(c->out_q, &s, 0) == pdTRUE) {
        send_text(c, s, true);
        free(s);
    }
    app_msg_t *m;
    while (xQueueReceive(c->evt_q, &m, 0) == pdTRUE) {
        send_text(c, m->line, true);
        free(m);
    }
}

static void auth_success(telnet_client_t *c)
{
    c->auth_pending = false;
    event_bus_subscribe(c->evt_q, SUB_TRAFFIC | SUB_SYS);

    char hello[256];
    snprintf(hello, sizeof(hello),
             "\r\n=== %s telnet v%s ===\r\n"
             "help - команды, monitor off - убрать трафик, raw on - ввод в UART\r\n> ",
             APP_NAME, APP_VERSION);
    send_text(c, hello, false);

    char msg[96];
    snprintf(msg, sizeof(msg), "telnet: подключился клиент #%d",
             (int)(c - s_clients) + 1);
    event_bus_publish_str(EV_SYS, msg);
}

static void client_close(telnet_client_t *c)
{
    if (!c->used) return;
    event_bus_unsubscribe(c->evt_q);
    vQueueDelete(c->evt_q);
    char *s;
    while (xQueueReceive(c->out_q, &s, 0) == pdTRUE) free(s);
    vQueueDelete(c->out_q);
    close(c->fd);
    memset(c, 0, sizeof(*c));
    c->fd = -1;
}

static void accept_client(void)
{
    struct sockaddr_in addr;
    socklen_t alen = sizeof(addr);
    int fd = accept(s_listen, (struct sockaddr *)&addr, &alen);
    if (fd < 0) return;

    int slot = -1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!s_clients[i].used) { slot = i; break; }
    }
    if (slot < 0) {
        const char *busy = "Слишком много telnet-клиентов\r\n";
        char *conv = charset_convert(busy);
        const char *out = conv ? conv : busy;
        send(fd, out, strlen(out), 0);
        free(conv);
        close(fd);
        return;
    }

    telnet_client_t *c = &s_clients[slot];
    memset(c, 0, sizeof(*c));
    c->fd = fd;
    c->used = true;
    c->monitor = true;
    c->echo = true;
    c->out_q = xQueueCreate(32, sizeof(char *));
    c->evt_q = xQueueCreate(64, sizeof(app_msg_t *));
    cli_session_init(&c->cli, c, telnet_out);

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    int keep = 1;
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keep, sizeof(keep));

    /* WILL ECHO, WILL SGA — клиент выключит локальный echo.
       Пока идёт ввод пароля, наш echo тоже выключен — пароль не видно. */
    const uint8_t neg[] = { IAC, WILL, OPT_ECHO, IAC, WILL, OPT_SGA };
    raw_send(c, neg, sizeof(neg));

    c->auth_pending = true;
    c->auth_deadline = esp_timer_get_time() / 1000 + 60000;
    send_text(c, "Password: ", false);
}

static void handle_byte(telnet_client_t *c, uint8_t b)
{
    /* ---------- Telnet IAC ---------- */
    if (c->iac_state == 1) {
        if (b == IAC) {                       /* экранированный 0xFF */
            c->iac_state = 0;
            if (c->len < TEL_LINE_MAX - 1) {
                c->line[c->len++] = (char)IAC;
                if (c->echo) raw_send(c, &b, 1);
            }
            return;
        }
        if (b == DO || b == DONT || b == WILL || b == WONT) {
            c->iac_cmd = b;
            c->iac_state = 2;
        } else if (b == SB) {
            c->iac_state = 3;                 /* подпереговоры: пропускаем */
        } else {
            c->iac_state = 0;                 /* NOP, GA, SE и т.п. */
        }
        return;
    }
    if (c->iac_state == 2) {
        /* Ответ всегда из 3 байт: IAC <команда> <опция>.
           Раньше отправлялось 2 байта, из-за чего клиент терял синхронизацию
           и выключал echo — вводимый текст не отображался. */
        uint8_t reply[3] = { IAC, WONT, b };
        if (c->iac_cmd == DO) {
            reply[1] = (b == OPT_ECHO || b == OPT_SGA) ? WILL : WONT;
            if (b == OPT_ECHO) c->echo = true;
        } else if (c->iac_cmd == DONT) {
            reply[1] = WONT;
            if (b == OPT_ECHO) c->echo = false;   /* клиент сам будет echo */
        } else if (c->iac_cmd == WILL) {
            reply[1] = DONT;                      /* не допускаем двойного echo */
        } else {                                  /* WONT */
            reply[1] = DONT;
        }
        raw_send(c, reply, sizeof(reply));
        c->iac_state = 0;
        return;
    }
    if (c->iac_state == 3) {                      /* тело подпереговоров */
        if (b == IAC) c->iac_state = 4;
        return;
    }
    if (c->iac_state == 4) {                      /* IAC внутри подпереговоров */
        c->iac_state = (b == SE) ? 0 : 3;
        return;
    }
    if (b == IAC) { c->iac_state = 1; return; }

    /* Пароль вводится без echo */
    if (c->auth_pending) {
        if (b == '\r') return;
        if (b == '\n') {
            c->pass[c->pass_len] = '\0';
            bool ok = auth_check_password(c->pass);
            c->pass_len = 0;
            if (ok) {
                auth_success(c);
            } else {
                send_text(c, "Неверный пароль", true);
                c->close_request = true;
            }
            return;
        }
        if (b == 0x7f || b == 0x08) {
            if (c->pass_len > 0) c->pass_len--;
            return;
        }
        if (c->pass_len < (int)sizeof(c->pass) - 1)
            c->pass[c->pass_len++] = (char)b;
        return;
    }

    if (b == '\r') return;
    if (b == '\n') {
        c->line[c->len] = '\0';
        char copy[TEL_LINE_MAX];
        snprintf(copy, sizeof(copy), "%s", c->line);
        c->len = 0;

        /* локальная команда мониторинга обрабатывается здесь */
        char *p = copy;
        while (*p == ' ' || *p == '\t' || *p == '/') p++;
        if (!strncasecmp(p, "monitor", 7)) {
            char *a = p + 7;
            while (*a == ' ' || *a == '\t') a++;
            bool on = !strncasecmp(a, "on", 2) || !strcmp(a, "1");
            if (!strncasecmp(a, "off", 3)) on = false;
            c->monitor = on;
            if (on) event_bus_subscribe(c->evt_q, SUB_TRAFFIC | SUB_SYS);
            else event_bus_unsubscribe(c->evt_q);
            put_line(c, on ? "monitor: on" : "monitor: off");
        } else {
            cli_process(&c->cli, copy);
        }
        put_line(c, ">");
        return;
    }
    if (b == 0x7f || b == 0x08) {
        if (c->len > 0) {
            c->len--;
            if (c->echo) {
                const char bs[] = "\b \b";
                raw_send(c, bs, 3);
            }
        }
        return;
    }
    if (c->len < TEL_LINE_MAX - 1) {
        c->line[c->len++] = (char)b;
        if (c->echo) raw_send(c, &b, 1);   /* локальный echo */
    }
}

static void telnet_task(void *arg)
{
    (void)arg;
    s_listen = socket(AF_INET, SOCK_STREAM, 0);
    if (s_listen < 0) { ESP_LOGE(TAG, "socket() failed"); return; }

    int one = 1;
    setsockopt(s_listen, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(TELNET_PORT);

    if (bind(s_listen, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(s_listen, 4) < 0) {
        ESP_LOGE(TAG, "bind/listen failed: errno=%d", errno);
        close(s_listen);
        s_listen = -1;
        return;
    }
    int flags = fcntl(s_listen, F_GETFL, 0);
    fcntl(s_listen, F_SETFL, flags | O_NONBLOCK);
    ESP_LOGI(TAG, "telnet listening on port %d", TELNET_PORT);

    for (int i = 0; i < MAX_CLIENTS; i++) s_clients[i].fd = -1;

    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        int maxfd = s_listen;
        FD_SET(s_listen, &rfds);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!s_clients[i].used) continue;
            FD_SET(s_clients[i].fd, &rfds);
            if (s_clients[i].fd > maxfd) maxfd = s_clients[i].fd;
        }

        struct timeval tv = { .tv_sec = 0, .tv_usec = 200000 };
        int r = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (r > 0 && FD_ISSET(s_listen, &rfds)) accept_client();

        for (int i = 0; i < MAX_CLIENTS; i++) {
            telnet_client_t *c = &s_clients[i];
            if (!c->used) continue;
            if (c->auth_pending &&
                esp_timer_get_time() / 1000 > c->auth_deadline) {
                client_close(c);
                continue;
            }
            drain_out(c);
            if (c->close_request) { client_close(c); continue; }
            if (FD_ISSET(c->fd, &rfds)) {
                uint8_t buf[128];
                int n = recv(c->fd, buf, sizeof(buf), 0);
                if (n <= 0) {
                    client_close(c);
                } else {
                    for (int j = 0; j < n; j++) handle_byte(c, buf[j]);
                    drain_out(c);
                    if (c->close_request) { client_close(c); continue; }
                }
            }
        }
    }
}

void telnet_init(void)
{
    xTaskCreate(telnet_task, "telnet", 8192, NULL, 6, NULL);
}
