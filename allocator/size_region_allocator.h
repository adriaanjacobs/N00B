#pragma once

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <vector>

#include "NOOB/config.h"

/**
 * Allocator to keep track of reserved/free space for arenas in a size region.
 */
class SizeRegionAllocator {
public:
    SizeRegionAllocator(uint8_t power);

    /**
     * Reserve space for an arena in the size region managed by this allocator. The minimum reserved
     * space depends on the system page size.
     *
     * @return The base pointer for the reserved arena space.
     */
    auto reserve() -> void*;

    /**
     * Get an available free block of memory the size of an arena for this power.
     *
     * @return A new free block of memory.
     */
    auto getOrCreateFree() -> void*;

    /**
     * Check whether there is an existing free block available to allocate again.
     *
     * @return Whether an existing free block is available for allocation.
     */
    auto existingFree() const -> bool;

    /**
     * Pop one entry from the free list and return it, modifying the free list head.
     *
     * @return The first item in the free list.
     */
    auto popExisting() -> void*;

    /**
     * Get the allocation in this size region at the given index.
     *
     * @param index The index of the allocation.
     * @return The requested allocation at the given index.
     */
    auto allocationAt(size_t index) const -> void*;

    /**
     * Add the given allocation index to the free list. The allocation is added as the new head of
     * the list.
     *
     * @param index The index to add to the free list.
     */
    void addToFreeList(size_t index);

    /**
     * The size of blocks managed and returned by this allocator.
     *
     * @return The size of an individual block.
     */
    auto allocationSize() const -> size_t;

    /**
     * Create a new free block and return it. The new block isn't added to the free list.
     *
     * @return The address of the corresponding new free allocation.
     */
    auto createNewFree() -> void*;

    /**
     * Get the index of the block for the given pointer.
     *
     * @param ptr Pointer to the start of a block in this size region.
     * @return The index of the block.
     */
    auto indexOf(const void* ptr) const -> size_t;

    /**
     * Give back reserved space inside the size region managed by this class. Space needs to be a
     * valid pointer to space that could have been reserved, but does not actually need to be
     * reserved.
     *
     * @param space Base pointer to the arena to unreserve.
     */
    void unreserve(void* space);

    /**
     * Get the power for which this size region allocator works.
     *
     * @return The power of the size region managed by this allocator.
     */
    auto getPower() const -> uint8_t;

    /**
     * Get the size region allocator for the given `power`.
     *
     * @param power The power of the size region for which to get the allocator.
     * @return Reference to the allocator for the size region with the given power.
     */
    static auto getInstance(uint8_t power) -> SizeRegionAllocator&;

private:
    /**
     * Status of the first `status->size()` arena-sized blocks in the size region.
     *   status_head     SIZE_MAX              status->size()
     *   |               |                     |
     * |---------------------------------------|
     * | 1 | 8 | x | x | / | x | x | x | 9 | 4 |
     * |---------------------------------------|
     *   0   1   2   3   4   5   6   7   8   9
     */
    std::vector<size_t> status;

    /**
     * Index of the first free block in the free list of the size region. If none, this is the max
     * value a `size_t` can hold.
     */
    size_t status_head = SIZE_MAX;

    /**
     * Power of the size region managed by this allocator.
     */
    uint8_t power;

    /**
     * Global lock.
     */
    std::mutex lock;
};
