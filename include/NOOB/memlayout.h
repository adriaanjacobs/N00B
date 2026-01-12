#pragma once

#include <NOOB/config.h>

#include <stdint.h>
#include <unistd.h>

inline uintptr_t size_region_base(uint8_t radix) {
    auto region_base = ((size_t) radix - NOOB_MIN_RADIX) << 42;
    // protect against mapping on the zero page
    if (region_base < 0x800000)
        region_base = 0x800000;
    return region_base;
}

inline size_t size_region_size() {
    return (1ULL << 42); 
}

inline size_t block_size(uint8_t radix) {
    return (1ULL << radix);
}

inline size_t single_arena_size(uint8_t radix) {
    return (1U << TAG_WIDTH) * block_size(radix);
}

inline size_t arith_area_size(uint8_t radix) {
    return single_arena_size(radix) << ARITH_LEEWAY_WIDTH;
}

inline uint8_t extract_radix(uintptr_t ptr) {
    return ((ptr >> 42) + NOOB_MIN_RADIX) & UINT8_MAX;
}

inline uint16_t extract_inpointertag(uintptr_t ptr) {
    auto radix = extract_radix(ptr);
    return (ptr >> radix) & ((1U << TAG_WIDTH) - 1);
}

inline uint16_t extract_toptag(uintptr_t ptr) {
    return ptr >> NOOB_TOPTAG_START;
}

inline size_t extract_offset(uintptr_t ptr) {
    auto radix = extract_radix(ptr);
    auto mask = (1ULL << radix) - 1;
    return ptr & mask;
}
