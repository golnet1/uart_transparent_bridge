#pragma once

#include <stdbool.h>
#include <stddef.h>

#define AUTH_DEFAULT_PASS "sec"
#define AUTH_PASS_MAX     32
#define AUTH_TOKEN_LEN   33   /* 16 случайных байт в HEX + '\0' */

void auth_init(void);

/* Пароль одновременно является путём в URL, поэтому только [A-Za-z0-9._~-]. */
bool auth_password_valid(const char *pass);

/* Проверка пароля (SHA-256 в NVS, открытым текстом не хранится). */
bool auth_check_password(const char *pass);
bool auth_set_password(const char *pass);

/* Успешный вход выдаёт токен для cookie. */
bool auth_login(const char *pass, char *token, size_t token_len);
bool auth_token_valid(const char *token);
void auth_logout(const char *token);
void auth_invalidate_all(void);

/* Резервная копия/восстановление хеша пароля (для экспорта настроек в файл). */
void        auth_export_hash_hex(char *out, size_t out_len);  /* 64 HEX + '\0' */
bool        auth_hash_hex_valid(const char *hex);
bool        auth_restore_hash_hex(const char *hex);           /* обновляет и cookie-токен */
const char *auth_current_token(void);
