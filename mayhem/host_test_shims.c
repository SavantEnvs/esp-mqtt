// Host stand-ins for the ESP-IDF services lib/platform.c needs when upstream's
// mqtt_outbox_host_test links lib/mqtt_msg (esp_random, esp_timer_get_time). On an
// IDF linux target these come from esp_hw_support/esp_timer; mayhem/esp_stubs/stubs.c also
// carries the fuzz-client transport, which the library-only suites must not link.
#include <stdint.h>
#include <stdlib.h>

uint32_t esp_random(void) { return (uint32_t)rand(); }
#include <time.h>
// platform_tick_get_ms -> esp_timer_get_time (microseconds since boot)
int64_t esp_timer_get_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}
