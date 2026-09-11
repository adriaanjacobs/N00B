#pragma once

#include <cstdint>
#include <mutex>
#include <optional>

#include "NOOB/config.h"
#include "arena.h"

/**
 * Each thread has multiple instances of these allocators, one for each size region. Each instance
 * holds a linked list of arenas with a fixed number of blocks. Each arena has a header which holds
 * the head of the arena's free list and a pointer to the next arena, in that order. Arena's are
 * lazily created. Before the first allocation, the first arena holds a `nullptr`. If an arena's
 * pointer to the next arena is the `nullptr`, that means there is no next arena. Similarily when
 * there are no free blocks in an arena, the pointer to the head of the free list is the `nullptr`.
 */
class ThreadSizeAllocator {
public:
    /// The number of equally-sized blocks in each arena. Not all of them can/should be used for
    /// allocations. For example the first one(s) holds the arena's header.
    static const constexpr int32_t BLOCKS_PER_ARENA = 1 << TAG_WIDTH;

    ThreadSizeAllocator(uint8_t power);
    ~ThreadSizeAllocator();

    /**
     * Check whether this allocator contains at least one arena.
     *
     * @return Whether the allocator contains an arena.
     */
    auto hasArenas() const -> bool;

    /**
     * Allocate a block in the size region managed by this allocator.
     */
    auto allocate() -> void*;

    void free(void* ptr);

    /**
     * `calloc()` in the size region managed by this allocator. Returns `nullptr` if allocation
     * failed.
     *
     * @return The new allocation.
     */
    auto zalloc() -> void*;

    /**
     * Check whether this allocator only has a single arena.
     *
     * @return Whether there is exactly one arena.
     */
    auto hasSingleArena() const -> bool;

private:
    /**
     * Power of the size region managed by this allocator.
     */
    uint8_t power;

    /**
     * Our first arena.
     */
    std::optional<Arena> first_arena = std::nullopt;

    /**
     * Our last arena.
     */
    std::optional<Arena> last_arena = std::nullopt;

    /**
     * The arena that last had free blocks. If we can allocate in an arena or we checked whether it
     * had free blocks and it did, we should update this variable. As long as we have an arena,
     * this should point to an arena! Even if that arena does not hold empty blocks, it should still
     * point to something.
     */
    std::optional<Arena> last_free = std::nullopt;

    /**
     * Arenas that were still in use by a thread after it finished running.
     */
    inline static std::optional<Arena> orphan_arenas[NON_NOOB_MIN_POWER - NOOB_MIN_POWER + 1];
    inline static std::mutex orphan_arenas_lock;

    /**
     * Get an arena that has free blocks.
     *
     * @return An arena with free blocks if there are available.
     */
    auto getFreeArena() -> std::optional<Arena>;

    /**
     * Get an arena with free blocks for allocation, or create a new one if no existing one could be
     * used.
     *
     * @return An arena with free blocks.
     */
    auto getOrCreateFreeArena() -> std::optional<Arena>;

    /**
     * Add the given arena to our list of arenas.
     *
     * @param arena The arena to add to our list.
     */
    void addArena(Arena& arena);

    /**
     * Create a new arena, add it to the linked list of arenas and return it. If no arena is
     * returned, none is created.
     *
     * @return The newly created arena.
     */
    auto createArena() -> std::optional<Arena>;

    /**
     * Move all the arenas of this allocator to the list of orphans.
     */
    void orphanArenas();

    /**
     * Try to get an arena for this allocator from the list of orphan arenas.
     *
     * @return An orphan arena if one is found.
     */
    auto recycleOrphanArena() const -> std::optional<Arena>;

    /**
     * Free the entire arena, removing it from this size allocator and handing its resources back to
     * the OS.
     *
     * @param arena The arena to free and remove.
     */
    void freeArena(Arena& arena);
};
