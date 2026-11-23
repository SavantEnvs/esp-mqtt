// Broker URI fuzzer: esp_mqtt_client_init() with a fuzzed broker.address.uri, then
// esp_mqtt_client_set_uri() with the same string on the live client. Reaches the URI parser
// (http_parser_parse_url, scheme/host/port/path/userinfo split, percent-decoding of credentials).
#include "sdkconfig.h"  // before mqtt_client.h, as the library sees it
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "mqtt_client.h"
#include "esp_stubs.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    // The API takes a NUL-terminated C string.
    char *uri = malloc(size + 1);
    if (!uri) return 0;
    memcpy(uri, data, size);
    uri[size] = 0;
    fz_reset(NULL, 0);
    esp_mqtt_client_config_t cfg = {0};
    cfg.broker.address.uri = uri;
    esp_mqtt_client_handle_t c = esp_mqtt_client_init(&cfg);
    if (c) {
        esp_mqtt_client_set_uri(c, uri);
        esp_mqtt_client_destroy(c);
    }
    free(uri);
    return 0;
}
