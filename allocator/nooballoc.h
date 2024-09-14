#pragma once

#include <stddef.h>

#define TAG_WIDTH (8)

#ifdef __cplusplus
extern "C" {
#endif

void noob_init(size_t max_radix, bool* inside);

void* noob_malloc(size_t nbytes) throw();

void noob_free(void* ptr) throw();

void* noob_realloc(void* oldptr, size_t newsize) throw();

#ifdef __cplusplus
}
#endif
