#pragma once

#include "config.h"

#include <stdint.h>
#include <unistd.h>

#define NUM_BLOCKS_IN_ARENA (1ULL << TAG_WIDTH)

inline uintptr_t size_region_base(uint8_t radix) { 
    return ((size_t) radix) << 42; 
}

inline size_t size_region_size() {
    return (1ULL << 42); 
}

inline size_t block_size(uint8_t radix) {
    return (1ULL << radix);
}

inline size_t single_arena_size(uint8_t radix) {
    return NUM_BLOCKS_IN_ARENA * block_size(radix);
}

inline uint8_t extract_radix(uintptr_t ptr) {
    return (ptr >> 42) & 0b0011'1111;
}

inline size_t extract_lowestMSBs(uintptr_t ptr) {
    auto radix = extract_radix(ptr);
    return (ptr >> radix) & (~0ULL >> (64 - TAG_WIDTH));
}

inline size_t extract_topbits(uintptr_t ptr) {
    return ptr >> (64 - TAG_WIDTH);
}

inline size_t extract_offset(uintptr_t ptr) {
    auto radix = extract_radix(ptr);
    auto mask = (1ULL << radix) - 1;
    return ptr & mask;
}

inline size_t extract_highestMSBs(uintptr_t ptr) {
    auto radix = extract_radix(ptr);
    ptr &= (~0ULL >> TAG_WIDTH); // clear the top bits
    ptr >>= radix + TAG_WIDTH; // shift away the offset + lowestMSBs
    return ptr;
}
