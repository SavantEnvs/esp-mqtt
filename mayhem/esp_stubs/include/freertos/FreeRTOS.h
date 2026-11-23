#pragma once
#include <stdint.h>
#include <stddef.h>
typedef int BaseType_t; typedef unsigned UBaseType_t; typedef uint32_t TickType_t; typedef uint32_t EventBits_t;
typedef void *TaskHandle_t; typedef void *SemaphoreHandle_t; typedef void *EventGroupHandle_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY 0xffffffffu
#define portTICK_PERIOD_MS 1
#define portTICK_RATE_MS 1
#define tskNO_AFFINITY 0x7fffffff
#define pdMS_TO_TICKS(x) (x)
typedef void (*TaskFunction_t)(void *);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t f, const char *n, uint32_t st, void *arg, UBaseType_t pr, TaskHandle_t *h, BaseType_t core);
void vTaskDelete(TaskHandle_t h);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void);
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t s, TickType_t t);
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t s);
void vSemaphoreDelete(SemaphoreHandle_t s);
EventGroupHandle_t xEventGroupCreate(void);
void vEventGroupDelete(EventGroupHandle_t g);
EventBits_t xEventGroupSetBits(EventGroupHandle_t g, EventBits_t b);
EventBits_t xEventGroupClearBits(EventGroupHandle_t g, EventBits_t b);
EventBits_t xEventGroupGetBits(EventGroupHandle_t g);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t g, EventBits_t b, BaseType_t clr, BaseType_t all, TickType_t t);
