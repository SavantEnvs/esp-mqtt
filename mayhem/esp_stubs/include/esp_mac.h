#pragma once
#include <stdint.h>
typedef enum { ESP_MAC_WIFI_STA, ESP_MAC_ETH, ESP_MAC_IEEE802154 } esp_mac_type_t;
int esp_read_mac(uint8_t *mac, esp_mac_type_t t);
