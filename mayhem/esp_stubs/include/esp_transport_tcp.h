#include "esp_transport.h"
esp_transport_handle_t esp_transport_tcp_init(void);
void esp_transport_tcp_set_keep_alive(esp_transport_handle_t t, esp_transport_keep_alive_t *k);
void esp_transport_tcp_set_interface_name(esp_transport_handle_t t, struct ifreq *i);
