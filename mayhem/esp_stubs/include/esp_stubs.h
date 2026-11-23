#pragma once
#include <stdint.h>
#include <stddef.h>
#include "mqtt_client.h"
#define FZ_OUT_MAX 65536
extern esp_mqtt_client_handle_t fz_client;
extern uint8_t fz_out[FZ_OUT_MAX];
extern size_t fz_out_len;
extern size_t fz_pos;
// Feed `data` as the broker's byte stream for the next client run and reset all stub state.
void fz_reset(const uint8_t *data, size_t len);
