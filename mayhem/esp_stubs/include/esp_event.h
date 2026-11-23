#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
typedef const char *esp_event_base_t; typedef void *esp_event_loop_handle_t;
typedef void (*esp_event_handler_t)(void *arg, esp_event_base_t base, int32_t id, void *data);
#define ESP_EVENT_ANY_ID -1
#define ESP_EVENT_DECLARE_BASE(id) extern esp_event_base_t const id
#define ESP_EVENT_DEFINE_BASE(id) esp_event_base_t const id = #id
typedef struct { int32_t queue_size; const char *task_name; UBaseType_t task_priority; uint32_t task_stack_size; BaseType_t task_core_id; } esp_event_loop_args_t;
esp_err_t esp_event_loop_create(const esp_event_loop_args_t *a, esp_event_loop_handle_t *l);
esp_err_t esp_event_loop_delete(esp_event_loop_handle_t l);
esp_err_t esp_event_loop_run(esp_event_loop_handle_t l, TickType_t t);
esp_err_t esp_event_post_to(esp_event_loop_handle_t l, esp_event_base_t b, int32_t id, const void *d, size_t sz, TickType_t t);
esp_err_t esp_event_handler_register_with(esp_event_loop_handle_t l, esp_event_base_t b, int32_t id, esp_event_handler_t h, void *arg);
esp_err_t esp_event_handler_unregister_with(esp_event_loop_handle_t l, esp_event_base_t b, int32_t id, esp_event_handler_t h);
