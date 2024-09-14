#pragma once

#include <NOOB/config.h>

#include <stddef.h>

void noob_non_allocating_printf(const char* fmt, ...);

void noob_init(size_t max_radix, bool* inside);

void* noob_malloc(size_t nbytes);

void noob_free(void* ptr);

void* noob_realloc(void* oldptr, size_t newsize);

void* noob_memalign(size_t alignment, size_t size);

void* noob_calloc(size_t nbytes);

size_t noob_usable_size(void* ptr);
