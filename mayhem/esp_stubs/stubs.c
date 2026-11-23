// Host stand-ins for the ESP-IDF services esp-mqtt uses (FreeRTOS, esp_event, tcp_transport,
// esp_random/esp_timer). They are not upstream code: esp-mqtt is an ESP-IDF component and these are
// the platform it runs on. Single-threaded: the MQTT task runs inline inside esp_mqtt_client_start(),
// events are dispatched synchronously, and the transport reads the broker byte stream from a buffer
// (fz_data/fz_len) and records what the client writes (fz_out).
#include "sdkconfig.h"  // before mqtt_client.h, as the library sees it
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_event.h"
#include "esp_transport.h"
#include "esp_transport_tcp.h"
#include "mqtt_client.h"
#include "esp_mac.h"
#include "esp_stubs.h"

const uint8_t *fz_data;
size_t fz_len, fz_pos;
esp_mqtt_client_handle_t fz_client;
uint8_t fz_out[FZ_OUT_MAX];
size_t fz_out_len;
static int fz_in_task, fz_stopping;
static unsigned fz_spins;
static int task_tag;

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t f, const char *n, uint32_t st, void *arg, UBaseType_t pr,
                                   TaskHandle_t *h, BaseType_t core)
{
    if (h) *h = &task_tag;
    fz_in_task = 1;
    f(arg);
    fz_in_task = 0;
    return pdTRUE;
}
void vTaskDelete(TaskHandle_t h) {}
// esp_mqtt_client_stop() refuses to run from the MQTT task, so report "another task" while stopping.
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (fz_in_task && !fz_stopping) ? (TaskHandle_t)&task_tag : NULL; }
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void) { return malloc(1); }
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t s, TickType_t t) { return pdTRUE; }
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t s) { return pdTRUE; }
void vSemaphoreDelete(SemaphoreHandle_t s) { free(s); }
EventGroupHandle_t xEventGroupCreate(void) { return calloc(1, sizeof(EventBits_t)); }
void vEventGroupDelete(EventGroupHandle_t g) { free(g); }
EventBits_t xEventGroupSetBits(EventGroupHandle_t g, EventBits_t b) { return *(EventBits_t *)g |= b; }
EventBits_t xEventGroupClearBits(EventGroupHandle_t g, EventBits_t b)
{
    EventBits_t o = *(EventBits_t *)g;
    *(EventBits_t *)g &= ~b;
    return o;
}
EventBits_t xEventGroupGetBits(EventGroupHandle_t g) { return *(EventBits_t *)g; }
// Reached while the task waits to reconnect (lock not held): the broker stream is over, so stop the
// client the way another task would. The spin cap bounds the number of reconnect rounds per input.
EventBits_t xEventGroupWaitBits(EventGroupHandle_t g, EventBits_t b, BaseType_t clr, BaseType_t all, TickType_t t)
{
    if (fz_in_task && !fz_stopping && (fz_pos >= fz_len || ++fz_spins > 4)) {
        fz_stopping = 1;
        esp_mqtt_client_stop(fz_client);
        fz_stopping = 0;
    }
    return *(EventBits_t *)g;
}

// esp_event: one handler per loop, dispatched synchronously on a copy of the event data
typedef struct { esp_event_handler_t h; void *arg; } loop_t;
esp_err_t esp_event_loop_create(const esp_event_loop_args_t *a, esp_event_loop_handle_t *l)
{
    *l = calloc(1, sizeof(loop_t));
    return *l ? ESP_OK : ESP_FAIL;
}
esp_err_t esp_event_loop_delete(esp_event_loop_handle_t l) { free(l); return ESP_OK; }
esp_err_t esp_event_loop_run(esp_event_loop_handle_t l, TickType_t t) { return ESP_OK; }
esp_err_t esp_event_post_to(esp_event_loop_handle_t l, esp_event_base_t b, int32_t id, const void *d, size_t sz, TickType_t t)
{
    loop_t *lp = l;
    if (lp->h) {
        void *c = malloc(sz ? sz : 1);
        memcpy(c, d, sz);
        lp->h(lp->arg, b, id, c);
        free(c);
    }
    return ESP_OK;
}
esp_err_t esp_event_handler_register_with(esp_event_loop_handle_t l, esp_event_base_t b, int32_t id, esp_event_handler_t h, void *arg)
{
    loop_t *lp = l;
    lp->h = h;
    lp->arg = arg;
    return ESP_OK;
}
esp_err_t esp_event_handler_unregister_with(esp_event_loop_handle_t l, esp_event_base_t b, int32_t id, esp_event_handler_t h)
{
    ((loop_t *)l)->h = NULL;
    return ESP_OK;
}

// transport: reads come from the input buffer, writes are recorded
struct esp_transport_item_t { int port; struct esp_tls_last_error err; };
struct esp_transport_list_t { esp_transport_handle_t t; };
esp_transport_list_handle_t esp_transport_list_init(void) { return calloc(1, sizeof(struct esp_transport_list_t)); }
esp_err_t esp_transport_list_destroy(esp_transport_list_handle_t l) { free(l->t); free(l); return ESP_OK; }
esp_err_t esp_transport_list_add(esp_transport_list_handle_t l, esp_transport_handle_t t, const char *s) { l->t = t; return ESP_OK; }
esp_transport_handle_t esp_transport_list_get_transport(esp_transport_list_handle_t l, const char *s) { return l->t; }
esp_transport_handle_t esp_transport_tcp_init(void) { return calloc(1, sizeof(struct esp_transport_item_t)); }
void esp_transport_tcp_set_keep_alive(esp_transport_handle_t t, esp_transport_keep_alive_t *k) {}
void esp_transport_tcp_set_interface_name(esp_transport_handle_t t, struct ifreq *i) {}
int esp_transport_connect(esp_transport_handle_t t, const char *host, int port, int timeout_ms) { return fz_pos < fz_len ? 0 : -1; }
int esp_transport_read(esp_transport_handle_t t, char *buf, int len, int timeout_ms)
{
    if (len <= 0) return 0;
    size_t n = fz_len - fz_pos;
    if (!n) return ERR_TCP_TRANSPORT_CONNECTION_CLOSED_BY_FIN;
    if (n > (size_t)len) n = len;
    memcpy(buf, fz_data + fz_pos, n);
    fz_pos += n;
    return (int)n;
}
int esp_transport_write(esp_transport_handle_t t, const char *buf, int len, int timeout_ms)
{
    size_t n = len > 0 ? (size_t)len : 0;
    if (n > FZ_OUT_MAX - fz_out_len) n = FZ_OUT_MAX - fz_out_len;
    memcpy(fz_out + fz_out_len, buf, n);
    fz_out_len += n;
    return len;
}
int esp_transport_poll_read(esp_transport_handle_t t, int timeout_ms) { return 1; }
int esp_transport_close(esp_transport_handle_t t) { return 0; }
esp_err_t esp_transport_destroy(esp_transport_handle_t t) { free(t); return ESP_OK; }
int esp_transport_get_default_port(esp_transport_handle_t t) { return t->port; }
esp_err_t esp_transport_set_default_port(esp_transport_handle_t t, int port) { t->port = port; return ESP_OK; }
int esp_transport_get_errno(esp_transport_handle_t t) { return 0; }
int esp_transport_sock_errno(esp_transport_handle_t t) { return 0; }
esp_tls_error_handle_t esp_transport_get_error_handle(esp_transport_handle_t t) { return &t->err; }
esp_err_t esp_tls_get_and_clear_last_error(esp_tls_error_handle_t h, int *code, int *flags)
{
    if (code) *code = 0;
    if (flags) *flags = 0;
    return ESP_OK;
}
// Deterministic counters so each input's run depends only on that input.
static uint32_t rnd;
uint32_t esp_random(void) { return ++rnd; }
static int64_t fz_now;
int64_t esp_timer_get_time(void) { return fz_now += 1000; }
int esp_read_mac(uint8_t *mac, esp_mac_type_t t) { memset(mac, 0x42, 6); return 0; }

void fz_reset(const uint8_t *data, size_t len)
{
    rnd = 0;
    fz_now = 1000000;
    fz_data = data;
    fz_len = len;
    fz_pos = 0;
    fz_spins = 0;
    fz_stopping = 0;
    fz_in_task = 0;
    fz_out_len = 0;
    fz_client = NULL;
}
