#pragma once

#include <NOOB/config.h>

#include <stdint.h>
#include <unistd.h>

/**
 * Get the smallest address where objects of size `2^power` can be mapped.
 *
 * @param power The power for the size of the object.
 * @return The lowest possible address for objects with given power.
 */
inline uintptr_t size_region_base(uint8_t power) {
    auto region_base = ((size_t) power - NOOB_MIN_POWER) << 42;
    // protect against mapping on the zero page
    if (region_base < 0x800000)
        region_base = 0x800000;
    return region_base;
}

/**
 * The size in bytes of N00B-managed regions. This is just an indication does
 * not necessarily represent the actual available space in every region.
 *
 * @return The size of every N00B-managed region.
 */
inline size_t size_region_size() {
    return (1ULL << 42);
}

/**
 * The size in bytes of objects with size `2^power`.
 *
 * @return The size of the objects.
 */
inline size_t block_size(uint8_t power) {
    return (1ULL << power);
}

/**
 * Amount of bytes in a single memory arena for objects with the given power.
 *
 * @param power The power for the objects in the arena.
 * @return The size in bytes of arena's holding those objects.
 */
inline size_t single_arena_size(uint8_t power) {
    return (1U << TAG_WIDTH) * block_size(power);
}

inline size_t arith_area_size(uint8_t power) {
    return single_arena_size(power);
}

/**
 * Get the power of the size region that holds the object pointed to by `ptr`.
 *
 * For example, if the object pointed to is 16 bytes, the power would be 4.
 * If the object pointed to is 44 bytes, the power would be 6.
 *
 * @param ptr The pointer to get the power of.
 * @return The power of the pointer.
 */
inline uint8_t extract_power(uintptr_t ptr) {
    return ((ptr >> 42) + NOOB_MIN_POWER) & UINT8_MAX;
}

/**
 * Get the IPTAG of the pointer.
 *
 * @param ptr The pointer to get the IPTAG of.
 * @return The IPTAG of the pointer.
 */
inline uint16_t extract_inpointertag(uintptr_t ptr) {
    auto power = extract_power(ptr);
    return (ptr >> power) & ((1U << TAG_WIDTH) - 1);
}

/**
 * Get the TOPTAG of the pointer.
 *
 * @param ptr The pointer to get the TOPTAG of.
 * @return The TOPTAG of the pointer.
 */
inline uint16_t extract_toptag(uintptr_t ptr) {
    return ptr >> NOOB_TOPTAG_START;
}

/**
 * Get the OFFSET of the pointer.
 *
 * @param ptr The pointer to get the OFFSET of.
 * @return The OFFSET of the pointer.
 */
inline size_t extract_offset(uintptr_t ptr) {
    auto power = extract_power(ptr);
    auto mask = (1ULL << power) - 1;
    return ptr & mask;
}

/**
 * Check whether the given pointer points to a N00B allocation in the N00B region of virtual memory.
 *
 * @param ptr Pointer to the allocation to check.
 * @return Whether the allocation is a N00B allocation.
 */
inline bool is_noob_allocation(void* ptr) {
    return ptr != NULL && extract_power((uintptr_t)ptr) < NON_NOOB_MIN_POWER;
}

/**
 * Clear the info about an object inside an arena (IPTAG and OFFSET) of the given N00B pointer.
 *
 * @param ptr The pointer to clear the inner arena bits of.
 * @return The `ptr` with the bits cleared.
 */
inline uintptr_t clear_inner_arena_info(uintptr_t ptr) {
    ptr &= ~(uint64_t)((1U << TAG_WIDTH) - 1) << extract_power(ptr);
    return ptr;
}
