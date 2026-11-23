#pragma once
#include <stdlib.h>
#define MALLOC_CAP_DEFAULT 0
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0
static inline void *heap_caps_calloc(size_t n, size_t s, unsigned c){(void)c;return calloc(n,s);}
static inline void *heap_caps_malloc(size_t s, unsigned c){(void)c;return malloc(s);}
static inline void *heap_caps_realloc(void*p,size_t s, unsigned c){(void)c;return realloc(p,s);}
