/**
 * @file noob_alloc.h
 *
 * Specialized C API to the N00B allocator, which can be used explicitly by programs when they know
 * they are using the N00B allocator.
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include "NOOB/memlayout.h"

/**
 * Strip the toptag bits of the given N00B pointer, setting them to 0.
 *
 * @param ptr The pointer to strip toptag bits of.
 * @return Pointer with top bits stripped.
 */
template <typename T>
auto noob_striptop(T* ptr) -> T* {
    uintptr_t _ptr = reinterpret_cast<uintptr_t>(ptr);
    _ptr &= (~0ULL >> (64 - NOOB_TOPTAG_START));
    return reinterpret_cast<T*>(_ptr);
}

/**
 * Malloc using the N00B allocator.
 *
 * @param nbytes Same as regular `malloc()`.
 * @return Same as regular `malloc()`.
 */
auto noob_malloc(size_t nbytes) -> void*;

/**
 * Free using the N00B allocator.
 *
 * @param ptr Same as regular `free()`.
 */
void noob_free(void* ptr);

/**
 * Realloc using the N00B allocator.
 *
 * @param oldptr Same as regular `realloc()`.
 * @param newsize Same as regular `realloc()`.
 * @return Same as regular `realloc()`.
 */
auto noob_realloc(void* oldptr, size_t newsize) -> void*;

/**
 * Memalign using the N00B allocator.
 *
 * @param alignment Same as regular `memalign()`
 * @param size Same as regular `memalign()`
 * @return Same as regular `memalign()`
 */
auto noob_memalign(size_t alignment, size_t size) -> void*;

/**
 * Calloc using the N00B allocator.
 *
 * @param nmemb Same as regular `calloc()`
 * @param size Same as regular `calloc()`
 * @return Same as regular `calloc()`
 */
auto noob_calloc(size_t nmemb, size_t size) -> void*;

/**
 * Get the size of the allocation for `ptr`.
 *
 * For example if this returns 16, it means that `ptr` points to an allocation of 16 bytes.
 *
 * @param ptr Same as regular `malloc_usable_size()`
 * @return Same as regular `malloc_usable_size()`
 */
auto noob_malloc_usable_size(void* ptr) -> size_t;
