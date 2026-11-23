// Broker -> client fuzzer: the input is the byte stream an MQTT broker sends to esp-mqtt.
// esp_mqtt_client_start() runs the client task inline (see esp_stubs/stubs.c); every
// esp_transport_read() is served from the input, so CONNACK, PUBLISH (incl. fragmented large
// payloads), SUBACK/UNSUBACK, PUBACK/PUBREC/PUBREL/PUBCOMP, PINGRESP and (v5) DISCONNECT/AUTH and
// all v5 properties are parsed by the client's own receive path.
// Built twice: -DFUZZ_PROTO=5 (fuzz_client_v5) and -DFUZZ_PROTO=311 (fuzz_client_v311).
// Byte 0 picks the receive buffer size (small sizes reach the fragmented-PUBLISH path) and, for
// the 3.x build, MQTT 3.1 vs 3.1.1; the rest is the broker stream.
#include "sdkconfig.h"  // before mqtt_client.h, as the library sees it
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include "mqtt_client.h"
#include "mqtt5_client.h"
#include "esp_stubs.h"

static volatile unsigned sink;

static void touch(const char *p, int len)
{
    for (int i = 0; p && i < len; i++) sink += (uint8_t)p[i];
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    esp_mqtt_event_handle_t e = data;
    esp_mqtt_client_handle_t c = e->client;
    touch(e->topic, e->topic_len);
    touch(e->data, e->data_len);
#if FUZZ_PROTO == 5
    esp_mqtt5_event_property_t *p = e->property;
    if (p) {
        touch(p->response_topic, p->response_topic_len);
        touch(p->correlation_data, p->correlation_data_len);
        touch(p->content_type, p->content_type_len);
        touch(p->server.response_info, p->server.response_info_len);
        uint8_t n = esp_mqtt5_client_get_user_property_count(p->user_property);
        if (n) {
            esp_mqtt5_user_property_item_t *items = calloc(n, sizeof(*items));
            uint8_t got = n;
            if (items && esp_mqtt5_client_get_user_property(p->user_property, items, &got) == ESP_OK) {
                for (int i = 0; i < got; i++) {
                    for (const char *k = items[i].key; k && *k; k++) sink += (uint8_t)*k;
                    for (const char *v = items[i].value; v && *v; v++) sink += (uint8_t)*v;
                    free((char *)items[i].key);
                    free((char *)items[i].value);
                }
            }
            free(items);
        }
    }
#endif
    if (id == MQTT_EVENT_CONNECTED) {
        // Leave pending message ids in the outbox for the broker's acks to match.
        esp_mqtt_client_subscribe_single(c, "a/b", 1);
        esp_mqtt_client_subscribe_single(c, "a/+", 2);
        esp_mqtt_client_publish(c, "a/q1", "x", 1, 1, 0);
        esp_mqtt_client_publish(c, "a/q2", "y", 1, 2, 0);
        esp_mqtt_client_unsubscribe(c, "a/c");
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1) return 0;
    static const int bufsz[4] = {64, 128, 1024, 4096};
    esp_mqtt_client_config_t cfg = {0};
    cfg.broker.address.uri = "mqtt://fuzz.host:1883";
#if FUZZ_PROTO == 5
    cfg.session.protocol_ver = MQTT_PROTOCOL_V_5;
#else
    cfg.session.protocol_ver = (data[0] & 4) ? MQTT_PROTOCOL_V_3_1 : MQTT_PROTOCOL_V_3_1_1;
#endif
    cfg.network.disable_auto_reconnect = true;
    cfg.buffer.size = bufsz[data[0] & 3];
    fz_reset(data + 1, size - 1);
    esp_mqtt_client_handle_t c = esp_mqtt_client_init(&cfg);
    if (!c) return 0;
    fz_client = c;
    esp_mqtt_client_register_event(c, MQTT_EVENT_ANY, on_event, NULL);
    esp_mqtt_client_start(c);
    esp_mqtt_client_destroy(c);
    return 0;
}
