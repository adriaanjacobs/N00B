#pragma once

#include <NOOB/config.h>

#include <stddef.h>
#include <stdint.h>

template<typename T>
T* noob_striptop(T* ptr) {
    return (T*) ((uintptr_t) ptr & (~0ULL >> TAG_WIDTH)); 
}

void noob_non_allocating_printf(const char* fmt, ...);

void noob_init(size_t max_radix, bool* inside);

void* noob_malloc(size_t nbytes);

void noob_free(void* ptr);

void* noob_realloc(void* oldptr, size_t newsize);

void* noob_memalign(size_t alignment, size_t size);

void* noob_calloc(size_t nbytes);

size_t noob_usable_size(void* ptr);

extern "C" void noob_allocate_stacks(void** stack_array, uint8_t lowest_radix, uint8_t highest_radix);
