#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
typedef struct esp_transport_item_t *esp_transport_handle_t; typedef struct esp_transport_list_t *esp_transport_list_handle_t;
typedef struct esp_tls_last_error *esp_tls_error_handle_t;
struct esp_tls_last_error { esp_err_t last_error; int esp_tls_error_code; int esp_tls_flags; };
typedef struct { bool keep_alive_enable; int keep_alive_idle, keep_alive_interval, keep_alive_count; } esp_transport_keep_alive_t;
struct psk_key_hint; struct ifreq;
esp_transport_list_handle_t esp_transport_list_init(void);
esp_err_t esp_transport_list_destroy(esp_transport_list_handle_t l);
esp_err_t esp_transport_list_add(esp_transport_list_handle_t l, esp_transport_handle_t t, const char *scheme);
esp_transport_handle_t esp_transport_list_get_transport(esp_transport_list_handle_t l, const char *scheme);
int esp_transport_connect(esp_transport_handle_t t, const char *host, int port, int timeout_ms);
int esp_transport_read(esp_transport_handle_t t, char *buf, int len, int timeout_ms);
int esp_transport_write(esp_transport_handle_t t, const char *buf, int len, int timeout_ms);
int esp_transport_poll_read(esp_transport_handle_t t, int timeout_ms);
int esp_transport_close(esp_transport_handle_t t);
esp_err_t esp_transport_destroy(esp_transport_handle_t t);
int esp_transport_get_default_port(esp_transport_handle_t t);
esp_err_t esp_transport_set_default_port(esp_transport_handle_t t, int port);
int esp_transport_get_errno(esp_transport_handle_t t);
esp_tls_error_handle_t esp_transport_get_error_handle(esp_transport_handle_t t);
#include <net/if.h>
enum esp_tcp_transport_err_t { ERR_TCP_TRANSPORT_NO_MEM=-3, ERR_TCP_TRANSPORT_CONNECTION_FAILED=-2, ERR_TCP_TRANSPORT_CONNECTION_CLOSED_BY_FIN=-1, ERR_TCP_TRANSPORT_CONNECTION_TIMEOUT=0 };
esp_err_t esp_tls_get_and_clear_last_error(esp_tls_error_handle_t h, int *code, int *flags);
int esp_transport_sock_errno(esp_transport_handle_t t);
