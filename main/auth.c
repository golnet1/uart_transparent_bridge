#include "auth.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mbedtls/sha256.h"
#include "nvs_flash.h"

#define AUTH_NVS_VERSION 1

static uint8_t s_hash[32];
static char s_token[AUTH_TOKEN_LEN];   /* детерминированный: первые 16 байт хеша */

static void hash_password(const char *pass, uint8_t out[32])
{
    mbedtls_sha256((const unsigned char *)pass, strlen(pass), out, 0);
}

static void update_token(void)
{
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 16; i++) {
        s_token[i * 2]     = hex[s_hash[i] >> 4];
        s_token[i * 2 + 1] = hex[s_hash[i] & 0x0F];
    }
    s_token[32] = '\0';
}

bool auth_password_valid(const char *pass)
{
    if (!pass) return false;
    size_t n = strlen(pass);
    if (n == 0 || n > AUTH_PASS_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        char c = pass[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' ||
              c == '.' || c == '~')) return false;
    }
    return true;
}

void auth_init(void)
{
    bool loaded = false;
    nvs_handle_t h;
    if (nvs_open("uart_moyka", NVS_READONLY, &h) == ESP_OK) {
        uint8_t ver = 0;
        size_t len = sizeof(s_hash);
        /* authver защищает от старого хеша пароля после обновления прошивки */
        if (nvs_get_u8(h, "authver", &ver) == ESP_OK &&
            ver == AUTH_NVS_VERSION &&
            nvs_get_blob(h, "auth", s_hash, &len) == ESP_OK &&
            len == sizeof(s_hash)) {
            loaded = true;
        }
        nvs_close(h);
    }
    if (!loaded) {
        hash_password(AUTH_DEFAULT_PASS, s_hash);
        if (nvs_open("uart_moyka", NVS_READWRITE, &h) == ESP_OK) {
            nvs_set_blob(h, "auth", s_hash, sizeof(s_hash));
            nvs_set_u8(h, "authver", AUTH_NVS_VERSION);
            nvs_commit(h);
            nvs_close(h);
        }
    }
    update_token();
}

bool auth_check_password(const char *pass)
{
    if (!pass) return false;
    uint8_t h[32];
    hash_password(pass, h);
    uint8_t diff = 0;
    for (int i = 0; i < 32; i++) diff |= (uint8_t)(h[i] ^ s_hash[i]);
    return diff == 0;
}

bool auth_set_password(const char *pass)
{
    if (!auth_password_valid(pass)) return false;
    hash_password(pass, s_hash);
    nvs_handle_t h;
    if (nvs_open("uart_moyka", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "auth", s_hash, sizeof(s_hash));
        nvs_set_u8(h, "authver", AUTH_NVS_VERSION);
        nvs_commit(h);
        nvs_close(h);
    }
    update_token();
    return true;
}

bool auth_login(const char *pass, char *token, size_t token_len)
{
    if (!auth_check_password(pass) || !token || token_len < AUTH_TOKEN_LEN)
        return false;
    snprintf(token, token_len, "%s", s_token);
    return true;
}

bool auth_token_valid(const char *token)
{
    return token && token[0] && !strcmp(token, s_token);
}

void auth_logout(const char *token)          { (void)token; }
void auth_invalidate_all(void)               { }

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void auth_export_hash_hex(char *out, size_t out_len)
{
    static const char hex[] = "0123456789abcdef";
    if (!out || out_len < 65) return;
    for (size_t i = 0; i < sizeof(s_hash); i++) {
        out[i * 2]     = hex[s_hash[i] >> 4];
        out[i * 2 + 1] = hex[s_hash[i] & 0x0F];
    }
    out[64] = '\0';
}

bool auth_hash_hex_valid(const char *hex)
{
    if (!hex || strlen(hex) != 64) return false;
    for (size_t i = 0; i < 64; i++)
        if (hex_nibble(hex[i]) < 0) return false;
    return true;
}

bool auth_restore_hash_hex(const char *hex)
{
    if (!auth_hash_hex_valid(hex)) return false;
    for (size_t i = 0; i < sizeof(s_hash); i++)
        s_hash[i] = (uint8_t)((hex_nibble(hex[i * 2]) << 4) | hex_nibble(hex[i * 2 + 1]));
    nvs_handle_t h;
    if (nvs_open("uart_moyka", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "auth", s_hash, sizeof(s_hash));
        nvs_set_u8(h, "authver", AUTH_NVS_VERSION);
        nvs_commit(h);
        nvs_close(h);
    }
    update_token();
    return true;
}

const char *auth_current_token(void)
{
    return s_token;
}
