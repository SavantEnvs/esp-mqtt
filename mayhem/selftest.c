// Known-answer tests for esp-mqtt, run by mayhem/test.sh. Each case drives the public client API
// against a scripted broker stream (esp_stubs/stubs.c) and checks the decoded events and the
// exact bytes the client wrote. Prints "PASS <name>" / "FAIL <name>" per check.
#include "sdkconfig.h"  // before mqtt_client.h, as the library sees it
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mqtt_client.h"
#include "mqtt5_client.h"
#include "esp_stubs.h"

static int passed, failed;
static void dump(void);
static void check(int ok, const char *name)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (ok) passed++; else { failed++; dump(); }
}

#define MAXEV 32
static struct { int id; char topic[64]; char data[512]; int data_len, total, offset, qos, nup; char upk[32], upv[32]; int rc; } ev[MAXEV];
static int nev;

static void dump(void)
{
    printf("  written:");
    for (size_t k = 0; k < fz_out_len && k < 128; k++) printf(" %02x", fz_out[k]);
    printf("\n");
    for (int k = 0; k < nev; k++)
        printf("  event %d topic='%s' data_len=%d total=%d rc=%d nup=%d\n", ev[k].id, ev[k].topic, ev[k].data_len, ev[k].total, ev[k].rc, ev[k].nup);
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    esp_mqtt_event_handle_t e = data;
    if (nev >= MAXEV) return;
    memset(&ev[nev], 0, sizeof(ev[nev]));
    ev[nev].id = id;
    if (e->topic && e->topic_len < 64) memcpy(ev[nev].topic, e->topic, e->topic_len);
    if (e->data && e->data_len < 512) memcpy(ev[nev].data, e->data, e->data_len);
    ev[nev].data_len = e->data_len;
    ev[nev].total = e->total_data_len;
    ev[nev].offset = e->current_data_offset;
    ev[nev].qos = e->qos;
    ev[nev].rc = e->reason_code;
    if (e->protocol_ver == MQTT_PROTOCOL_V_5 && e->property && e->property->user_property) {
        esp_mqtt5_user_property_item_t it[4];
        uint8_t n = 4;
        if (esp_mqtt5_client_get_user_property(e->property->user_property, it, &n) == ESP_OK) {
            ev[nev].nup = n;
            for (int i = 0; i < n; i++) {
                if (i == 0) {
                    snprintf(ev[nev].upk, 32, "%s", it[i].key);
                    snprintf(ev[nev].upv, 32, "%s", it[i].value);
                }
                free((char *)it[i].key);
                free((char *)it[i].value);
            }
        }
    }
    nev++;
    if (id == MQTT_EVENT_CONNECTED) esp_mqtt_client_subscribe_single(e->client, "t/#", 0);
}

static void run(esp_mqtt_protocol_ver_t ver, int bufsz, const uint8_t *stream, size_t len)
{
    nev = 0;
    fz_reset(stream, len);
    esp_mqtt_client_config_t cfg = {0};
    cfg.broker.address.uri = "mqtt://broker.test:1883";
    cfg.credentials.client_id = "selftest";
    cfg.session.protocol_ver = ver;
    cfg.session.keepalive = 60;
    cfg.network.disable_auto_reconnect = true;
    cfg.buffer.size = bufsz;
    esp_mqtt_client_handle_t c = esp_mqtt_client_init(&cfg);
    if (!c) { printf("init failed\n"); return; }
    fz_client = c;
    esp_mqtt_client_register_event(c, MQTT_EVENT_ANY, on_event, NULL);
    esp_mqtt_client_start(c);
    esp_mqtt_client_destroy(c);
}

static int find(int id, int from)
{
    for (int i = from; i < nev; i++) if (ev[i].id == id) return i;
    return -1;
}

int main(void)
{
    // MQTT 3.1.1: CONNACK, then PUBLISH qos0 "t/x" = "hello"
    static const uint8_t s1[] = {0x20, 0x02, 0x00, 0x00, 0x30, 0x0a, 0x00, 0x03, 't', '/', 'x', 'h', 'e', 'l', 'l', 'o'};
    run(MQTT_PROTOCOL_V_3_1_1, 1024, s1, sizeof s1);
    static const uint8_t connect311[] = {0x10, 0x14, 0x00, 0x04, 'M', 'Q', 'T', 'T', 0x04, 0x02, 0x00, 0x3c,
                                         0x00, 0x08, 's', 'e', 'l', 'f', 't', 'e', 's', 't'};
    check(fz_out_len >= sizeof connect311 && !memcmp(fz_out, connect311, sizeof connect311), "v311_connect_bytes");
    int i = find(MQTT_EVENT_CONNECTED, 0);
    check(i >= 0, "v311_connected_event");
    int d = find(MQTT_EVENT_DATA, 0);
    check(d > i && !strcmp(ev[d].topic, "t/x") && ev[d].data_len == 5 && !memcmp(ev[d].data, "hello", 5), "v311_publish_topic_payload");
    // the SUBSCRIBE sent from the CONNECTED handler follows the CONNECT
    // (message id 1 comes from the deterministic esp_random stub)
    check(fz_out_len >= sizeof connect311 + 10 &&
          !memcmp(fz_out + sizeof connect311, "\x82\x08\x00\x01\x00\x03t/#\x00", 10), "v311_subscribe_bytes");

    // CONNACK refused (return code 5, not authorized) -> error event, no CONNECTED
    static const uint8_t s2[] = {0x20, 0x02, 0x00, 0x05};
    run(MQTT_PROTOCOL_V_3_1_1, 1024, s2, sizeof s2);
    check(find(MQTT_EVENT_CONNECTED, 0) < 0 && find(MQTT_EVENT_ERROR, 0) >= 0, "v311_connack_refused");

    // 200-byte payload, two-byte remaining length, 64-byte buffer -> fragmented DATA events
    uint8_t s3[4 + 3 + 5 + 200];
    size_t n = 0;
    memcpy(s3, "\x20\x02\x00\x00", 4); n = 4;
    s3[n++] = 0x30; s3[n++] = (5 + 200) % 128 | 0x80; s3[n++] = (5 + 200) / 128;
    memcpy(s3 + n, "\x00\x03t/y", 5); n += 5;
    for (int k = 0; k < 200; k++) s3[n++] = (uint8_t)('a' + k % 26);
    run(MQTT_PROTOCOL_V_3_1_1, 64, s3, n);
    int frags = 0, sum = 0, ok = 1;
    for (int k = 0; k < nev; k++) if (ev[k].id == MQTT_EVENT_DATA) {
        frags++;
        if (ev[k].total != 200 || ev[k].offset != sum) ok = 0;
        for (int b = 0; b < ev[k].data_len && b < 512; b++) if (ev[k].data[b] != 'a' + (sum + b) % 26) ok = 0;
        sum += ev[k].data_len;
    }
    check(ok && frags > 1 && sum == 200, "v311_fragmented_publish");

    // MQTT 5: CONNACK with topic-alias-maximum property, PUBLISH with a user property
    static const uint8_t s4[] = {0x20, 0x06, 0x00, 0x00, 0x03, 0x22, 0x00, 0x05,
                                 0x30, 0x12, 0x00, 0x03, 't', '/', 'z', 0x09, 0x26, 0x00, 0x01, 'k', 0x00, 0x03, 'v', 'a', 'l', 'h', 'e', 'y'};
    run(MQTT_PROTOCOL_V_5, 1024, s4, sizeof s4);
    check(fz_out_len > 9 && fz_out[0] == 0x10 && !memcmp(fz_out + 2, "\x00\x04MQTT\x05", 7), "v5_connect_bytes");
    i = find(MQTT_EVENT_CONNECTED, 0);
    check(i >= 0, "v5_connected_event");
    d = find(MQTT_EVENT_DATA, 0);
    check(d > i && !strcmp(ev[d].topic, "t/z") && ev[d].data_len == 3 && !memcmp(ev[d].data, "hey", 3), "v5_publish_topic_payload");
    check(d >= 0 && ev[d].nup == 1 && !strcmp(ev[d].upk, "k") && !strcmp(ev[d].upv, "val"), "v5_user_property");

    // MQTT 5 CONNACK with a property length running past the packet must not connect
    static const uint8_t s5[] = {0x20, 0x04, 0x00, 0x00, 0x09, 0x22};
    run(MQTT_PROTOCOL_V_5, 1024, s5, sizeof s5);
    check(find(MQTT_EVENT_CONNECTED, 0) < 0, "v5_connack_bad_property_len");

    printf("selftest: %d passed, %d failed\n", passed, failed);
    return failed != 0;
}
