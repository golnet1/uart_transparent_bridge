#pragma once

/* 0 = UTF-8 (как есть), 1 = CP866 (для Windows-терминалов с кириллицей) */
void charset_init(void);
int  charset_get(void);
void charset_set(int cp866);

/* Возвращает malloc-строку; вызывающий обязан free(). */
char *charset_convert(const char *src);
