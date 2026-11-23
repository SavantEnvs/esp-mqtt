// Outbound encoder fuzzer: fuzzed client configuration (client id, credentials, last will, keepalive,
// protocol version, buffer size, v5 connect/user properties) and fuzzed arguments to the publish /
// enqueue / subscribe / unsubscribe API, called from the MQTT_EVENT_CONNECTED handler after the
// stubbed broker answers with a valid CONNACK. Reaches mqtt_msg_connect/publish/subscribe/unsubscribe,
// the v5 property and user-property serialisers, buffer growth and outbox enqueue.
// Input layout: a sequence of fields; a string field is one length byte followed by that many bytes.
#include "sdkconfig.h"  // before mqtt_client.h, as the library sees it
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "mqtt_client.h"
#include "mqtt5_client.h"
#include "esp_stubs.h"

typedef struct { const uint8_t *p; size_t n; char *strs[64]; int ns; } prov_t;

static uint8_t take_u8(prov_t *d)
{
    if (!d->n) return 0;
    d->n--;
    return *d->p++;
}
static uint16_t take_u16(prov_t *d) { return (uint16_t)(take_u8(d) | (take_u8(d) << 8)); }
// Returns a heap copy (NUL-terminated) or NULL when the length byte is 0xff; *len gets its length.
static char *take_str(prov_t *d, int *len)
{
    uint8_t l = take_u8(d);
    if (l == 0xff || d->ns >= 64) { if (len) *len = 0; return NULL; }
    if (l > d->n) l = (uint8_t)d->n;
    char *s = malloc(l + 1);
    memcpy(s, d->p, l);
    s[l] = 0;
    d->p += l;
    d->n -= l;
    d->strs[d->ns++] = s;
    if (len) *len = l;
    return s;
}

static prov_t g;
static int g_v5;
// The client keeps a pointer to a staged v5 property until the next matching API call consumes it,
// so the property structs and user-property lists live until the client is destroyed.
static esp_mqtt5_publish_property_config_t g_pp;
static esp_mqtt5_subscribe_property_config_t g_sp;
static esp_mqtt5_unsubscribe_property_config_t g_uq;
static mqtt5_user_property_handle_t g_ups[64];
static int g_nups;

static mqtt5_user_property_handle_t take_user_props(prov_t *d)
{
    mqtt5_user_property_handle_t h = NULL;
    esp_mqtt5_user_property_item_t items[4];
    uint8_t n = take_u8(d) % 5;
    for (int i = 0; i < n; i++) {
        items[i].key = take_str(d, NULL);
        items[i].value = take_str(d, NULL);
    }
    if (n && g_nups < 64 && esp_mqtt5_client_set_user_property(&h, items, n) == ESP_OK) g_ups[g_nups++] = h;
    else if (h) esp_mqtt5_client_delete_user_property(h), h = NULL;
    return h;
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    esp_mqtt_event_handle_t e = data;
    esp_mqtt_client_handle_t c = e->client;
    if (id != MQTT_EVENT_CONNECTED) return;
    prov_t *d = &g;
    while (d->n) {
        uint8_t op = take_u8(d);
        int qos = (op >> 3) & 3, retain = (op >> 5) & 1;
        mqtt5_user_property_handle_t up = NULL;
        if (g_v5 && (op & 0x80)) {
            up = take_user_props(d);
            switch (op & 7) {
            case 0: case 1: {
                esp_mqtt5_publish_property_config_t pp = {0};
                esp_mqtt5_publish_property_config_t *ppp = &g_pp;
                int cl = 0;
                pp.payload_format_indicator = op & 0x40;
                pp.message_expiry_interval = take_u16(d);
                pp.topic_alias = take_u16(d);
                pp.response_topic = take_str(d, NULL);
                pp.correlation_data = take_str(d, &cl);
                pp.correlation_data_len = cl;
                pp.content_type = take_str(d, NULL);
                pp.user_property = up;
                *ppp = pp;
                esp_mqtt5_client_set_publish_property(c, ppp);
                break;
            }
            case 2: case 3: {
                esp_mqtt5_subscribe_property_config_t sp = {0};
                sp.subscribe_id = take_u16(d);
                sp.no_local_flag = op & 0x40;
                sp.retain_as_published_flag = op & 0x20;
                sp.retain_handle = op & 3;
                sp.share_name = take_str(d, NULL);
                sp.is_share_subscribe = sp.share_name != NULL;
                sp.user_property = up;
                g_sp = sp;
                esp_mqtt5_client_set_subscribe_property(c, &g_sp);
                break;
            }
            case 4: {
                esp_mqtt5_unsubscribe_property_config_t uq = {0};
                uq.share_name = take_str(d, NULL);
                uq.is_share_subscribe = uq.share_name != NULL;
                uq.user_property = up;
                g_uq = uq;
                esp_mqtt5_client_set_unsubscribe_property(c, &g_uq);
                break;
            }
            }
        }
        switch (op & 7) {
        case 0: {
            int len;
            const char *topic = take_str(d, NULL);
            const char *payload = take_str(d, &len);
            esp_mqtt_client_publish(c, topic, payload, (op & 0x40) ? 0 : len, qos, retain);
            break;
        }
        case 1: {
            int len;
            const char *topic = take_str(d, NULL);
            const char *payload = take_str(d, &len);
            esp_mqtt_client_enqueue(c, topic, payload, len, qos, retain, op & 0x40);
            break;
        }
        case 2: {
            const char *topic = take_str(d, NULL);
            if (topic) esp_mqtt_client_subscribe_single(c, topic, qos);
            break;
        }
        case 3: {
            esp_mqtt_topic_t list[4];
            int n = 1 + (take_u8(d) & 3), k = 0;
            for (int i = 0; i < n; i++) {
                list[k].filter = take_str(d, NULL);
                list[k].qos = take_u8(d) & 3;
                if (list[k].filter) k++;
            }
            if (k) esp_mqtt_client_subscribe_multiple(c, list, k);
            break;
        }
        case 4: {
            const char *topic = take_str(d, NULL);
            if (topic) esp_mqtt_client_unsubscribe(c, topic);
            break;
        }
        default:
            esp_mqtt_client_get_outbox_size(c);
            break;
        }
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static const uint8_t connack_v3[] = {0x20, 0x02, 0x00, 0x00};
    static const uint8_t connack_v5[] = {0x20, 0x03, 0x00, 0x00, 0x00};
    memset(&g, 0, sizeof(g));
    g_nups = 0;
    g.p = data;
    g.n = size;
    uint8_t flags = take_u8(&g);
    esp_mqtt_client_config_t cfg = {0};
    static const int bufsz[4] = {32, 128, 1024, 4096};
    static const esp_mqtt_protocol_ver_t vers[4] = {MQTT_PROTOCOL_V_3_1, MQTT_PROTOCOL_V_3_1_1, MQTT_PROTOCOL_V_5, MQTT_PROTOCOL_V_5};
    cfg.broker.address.uri = "mqtt://fuzz.host:1883";
    cfg.session.protocol_ver = vers[flags & 3];
    g_v5 = cfg.session.protocol_ver == MQTT_PROTOCOL_V_5;
    cfg.buffer.size = bufsz[(flags >> 2) & 3];
    cfg.buffer.out_size = (flags & 0x10) ? bufsz[(flags >> 2) & 3] / 2 + 16 : 0;
    cfg.network.disable_auto_reconnect = true;
    cfg.session.disable_clean_session = flags & 0x20;
    cfg.credentials.set_null_client_id = flags & 0x40;
    cfg.credentials.client_id = take_str(&g, NULL);
    cfg.credentials.username = take_str(&g, NULL);
    cfg.credentials.authentication.password = take_str(&g, NULL);
    cfg.session.last_will.topic = take_str(&g, NULL);
    cfg.session.last_will.msg = take_str(&g, &cfg.session.last_will.msg_len);
    if (flags & 0x80) cfg.session.last_will.msg_len = 0;
    uint8_t w = take_u8(&g);
    cfg.session.last_will.qos = w & 3;
    cfg.session.last_will.retain = (w >> 2) & 1;
    cfg.session.keepalive = take_u16(&g);

    fz_reset(g_v5 ? connack_v5 : connack_v3, g_v5 ? sizeof(connack_v5) : sizeof(connack_v3));
    esp_mqtt_client_handle_t c = esp_mqtt_client_init(&cfg);
    if (c) {
        if (g_v5) {
            esp_mqtt5_connection_property_config_t cp = {0};
            int cl = 0;
            cp.session_expiry_interval = take_u16(&g);
            cp.maximum_packet_size = take_u16(&g);
            cp.receive_maximum = take_u16(&g);
            cp.topic_alias_maximum = take_u16(&g);
            cp.request_resp_info = w & 8;
            cp.request_problem_info = w & 16;
            cp.will_delay_interval = take_u16(&g);
            cp.message_expiry_interval = take_u16(&g);
            cp.payload_format_indicator = w & 32;
            cp.content_type = take_str(&g, NULL);
            cp.response_topic = take_str(&g, NULL);
            cp.correlation_data = take_str(&g, &cl);
            cp.correlation_data_len = cl;
            cp.user_property = take_user_props(&g);
            cp.will_user_property = take_user_props(&g);
            esp_mqtt5_client_set_connect_property((esp_mqtt5_client_handle_t)c, &cp);
        }
        fz_client = c;
        esp_mqtt_client_register_event(c, MQTT_EVENT_ANY, on_event, NULL);
        esp_mqtt_client_start(c);
        esp_mqtt_client_destroy(c);
    }
    for (int i = 0; i < g_nups; i++) esp_mqtt5_client_delete_user_property(g_ups[i]);
    for (int i = 0; i < g.ns; i++) free(g.strs[i]);
    return 0;
}
