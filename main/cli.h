#pragma once

#include "app.h"

typedef struct {
    int   target;    /* 1, 2, 3 = оба */
    bool  raw;       /* весь ввод уходит на UART без разбора команд */
    void *ctx;
    void (*out)(void *ctx, const char *line);
} cli_session_t;

void cli_init_usb(void);
void cli_session_init(cli_session_t *s, void *ctx,
                      void (*out)(void *ctx, const char *line));
void cli_process(cli_session_t *s, char *line);   /* line изменяется */
void cli_usb_bytes(const uint8_t *data, size_t len);
