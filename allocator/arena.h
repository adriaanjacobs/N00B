/**
 * Description of the N00B memory arena and functionality to work with them.
 *
 * @file arena.h
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "NOOB/config.h"

/**
 * Represents a N00B memory arena in a size region. Internally keeps a pointer to the associated
 * header at the start of the arena.
 */
class Arena {
public:
    /**
     * The smallest size that should be reserved for an Arena header. Since we need to reserve a
     * cache line in each N00B arena, the header should fit in that space.
     */
    static const constexpr size_t MIN_HEADER_SIZE = CACHE_LINE_SIZE;

    /**
     * The number of equally-sized blocks in each arena. Not all of them can/should be used for
     * allocations. For example the first one(s) holds the arena's header.
     */
    static const constexpr int32_t BLOCKS_PER_ARENA = 1 << TAG_WIDTH;

    /**
     * Construct a new arena with the given base.
     *
     * @param base The base address of the arena.
     */
    Arena(void* base);

    /**
     * Initialize the underlying memory representing the arena. This initializes the header and
     * creates the initial free list. The arena is automatically adopted by the current thread.
     */
    void initialize();

    /**
     * Check whether the arena is fully empty. The thread that owns the arena is the only one that
     * can allocate in it. It is therefore the only one that may use this check to ensure the arena
     * can be safely cleaned up.
     *
     * @return Whether the arena has no allocated blocks.
     */
    auto isEmpty() const -> bool;

    /**
     * Check whether the arena has no free blocks that can be allocated.
     *
     * @return Whether all blocks in the arena are allocated.
     */
    auto isFull() const -> bool;

    /**
     * Check whether this arena is mine or someone else's.
     *
     * @return Gives `true` if this arena belongs to the current thread.
     */
    auto mine() const -> bool;

    /**
     * Assign the arena to the current thread.
     */
    void adopt();

    /**
     * Turn this arena into an orphan.
     */
    void orphan();

    /**
     * Check whether this arena contains the given `ptr`. This does not guarantee that `ptr` is a
     * valid allocation, only that it falls inside of this arena.
     *
     * @param ptr The pointer to check.
     * @return Whether `ptr` is inside this arena.
     */
    auto contains(const void* ptr) const -> bool;

    /**
     * Get the arena that would contain the given `ptr`. Does not actually check whether the arena
     * exists.
     *
     * @param ptr The pointer to get the arena for.
     * @return The arena that contains `ptr`.
     */
    static auto arenaOf(const void* ptr) -> Arena;

    /**
     * Get the base of this arena.
     *
     * @return The base address of the arena.
     */
    auto base() const -> void*;

    /**
     * Allocate a block from the arena. Returns `nullptr` if no free blocks were available.
     *
     * @return The base of the new block.
     */
    auto allocate() -> void*;

    /**
     * Free the given block in the arena. If `ptr` is `nullptr`, this does nothing. If `ptr` points
     * to space not allocated in this arena, undefined behavior occurs. `ptr` must point to the
     * start of its allocation, otherwise undefined behavior occurs.
     *
     * @param ptr The block to free.
     */
    void free(const void* ptr);

    /**
     * Free the given block in the arena from an external thread. If `ptr` is `nullptr`, this does
     * nothing. If `ptr` points to space not allocated in this arena, undefined behavior occurs.
     *
     * @param ptr The block to free.
     */
    void externalFree(const void* ptr);

    /**
     * Get the index of the block in the given arena. The `ptr` must be inside the arena but may be
     * anywhere inside of a block.
     *
     * @param ptr The pointer to get the block index for.
     * @return The index of the block containing the pointer.
     */
    size_t blockIndex(const void* ptr) const;

    /**
     * Get the offset in bytes of the pointer to the base of the given arena. The `ptr` must be in
     * the arena.
     *
     * @param ptr The pointer to get the offset to.
     * @return The offset from the base of the arena to the `ptr`.
     */
    auto pointerOffset(const void* ptr) const -> intptr_t;

    /**
     * Set the given arena as the next arena.
     *
     * @param arena The arena to set as the next arena.
     */
    void setNext(const std::optional<Arena> arena);

    /**
     * Set the given arena as the previous arena.
     *
     * @param arena The arena to set as the previous arena.
     */
    void setPrev(const std::optional<Arena> arena);

    /**
     * Get the next arena in the linked list of arenas.
     *
     * @return The next arena if one is available.
     */
    auto nextArena() const -> std::optional<Arena>;

    /**
     * Get the previous arena in the linked list of arenas.
     *
     * @return The previous arena if one is available.
     */
    auto prevArena() const -> std::optional<Arena>;

    /**
     * Index of the first allocatable block in an arena.
     *
     * @return Index of the first allocatable block in an arena.
     */
    auto firstAllocatableBlockIndex() const -> size_t;

    /**
     * Index of the last allocatable block in an arena.
     *
     * @return Index of the last allocatable block in an arena.
     */
    auto lastAllocatableBlockIndex() const -> size_t;

    /**
     * How many blocks there are available for allocation in total.
     *
     * @return How many blocks in this arena can be used for allocation.
     */
    auto allocatableBlocks() const -> int32_t;

    /**
     * Number of blocks used for padding in this arena.
     *
     * @return Number of blocks reserved for padding.
     */
    auto paddingBlocks() const -> int32_t;

    /**
     * The power of the object size for this arena.
     *
     * @return The power of the arena.
     */
    auto power() const -> uint8_t;

    /**
     * Check whether `ptr` is aligned to a block for the arena. The block does not need to be
     * allocated and does not need to be in the arena.
     *
     * @param ptr The pointer to check alignment for.
     * @return `true` if `ptr` is aligned to a block.
     */
    auto alignedToBlock(const void* ptr) const -> bool;

    /**
     * A block in the free list.
     */
    struct Block {
        Block* next_block;
    };

    /**
     * Header at the start of each arena.
     */
    struct Header {
        /**
         * Previous arena in the linked list, or `nullptr` if this is the first arena.
         */
        void* prev_arena;

        /**
         * Next arena in the linked list, or `nullptr` if this is the last arena.
         */
        void* next_arena;

        /**
         * Id of the thread this arena belongs to.
         */
        size_t thread_id;

        /**
         * The local free list.
         */
        Block* free_list_head;

        /**
         * The free list that external threads operate on.
         */
        std::atomic<Block*> thread_free_list_head;

        /**
         * Number of free blocks in the local free list.
         */
        int32_t free_count;

        /**
         * Number of free blocks in the external free list.
         */
        std::atomic_int32_t thread_free_count;
    };
    static_assert(sizeof(Header) <= Arena::MIN_HEADER_SIZE,
        "Arena header does not fit in available header space");

private:
    /**
     * Pointer to the header, which lives in the first block(s) of an arena. It can therefore also
     * be used to get the base address of the arena.
     */
    Header* header;

    /**
     * Get the block at the given index, starting from the arena base.
     *
     * @param index The index of the arena block.
     * @return Pointer to the requested block.
     */
    auto blockAt(size_t index) const -> Block*;

    /**
     * Get and remove the first free block in the thread-local free list.
     *
     * @return Pointer to the new block or `nullptr` is none.
     */
    auto allocateBlock() -> Block*;

    /**
     * Collect the current blocks in the external free list into the local one. This should only be
     * called by the owning thread!
     */
    void collectExternalFreeList();
};
