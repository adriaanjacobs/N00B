#include "arena.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <thread>

#include "NOOB/config.h"
#include "NOOB/memlayout.h"
#include "noob_alloc.h"
#if NOOB_TAG_POINTERS
#include "util.h"
#endif
#if NOOBALLOC_METRICS
#include "thread_allocator.h"
#endif

Arena::Arena(void* base)
    : header { static_cast<Header*>(base) } { }

void Arena::initialize() {
    Block* block = blockAt(firstAllocatableBlockIndex());

    for (size_t block_index = firstAllocatableBlockIndex();
         block_index < lastAllocatableBlockIndex(); block_index++) {
        block->next_block =
            reinterpret_cast<Block*>(reinterpret_cast<uintptr_t>(block) + block_size(power()));
        block = block->next_block;
    }

    block->next_block = nullptr;
    header->prev_arena = nullptr;
    header->next_arena = nullptr;
    this->adopt();
    header->free_list_head = blockAt(firstAllocatableBlockIndex());
    header->thread_free_list_head.store(nullptr);
    header->free_count = allocatableBlocks();
    header->thread_free_count.store(0);
}

auto Arena::isEmpty() const -> bool {
    return header->free_count + header->thread_free_count.load(std::memory_order::seq_cst)
        == allocatableBlocks();
}

auto Arena::isFull() const -> bool {
    return header->free_list_head == nullptr
        && header->thread_free_list_head.load(std::memory_order::seq_cst) == nullptr;
}

auto Arena::mine() const -> bool {
    return header->thread_id == std::hash<std::thread::id> {}(std::this_thread::get_id());
}

void Arena::orphan() {
    header->thread_id = 0;
}

void Arena::adopt() {
    header->thread_id = std::hash<std::thread::id> {}(std::this_thread::get_id());
}

auto Arena::base() const -> void* {
    return header;
}

auto Arena::allocate() -> void* {
    Block* new_block = allocateBlock();

    if (new_block == nullptr) {
        collectExternalFreeList();
        new_block = allocateBlock();
    }

#if NOOB_TAG_POINTERS
    new_block =
        reinterpret_cast<Block*>(noob_embed_top_tag(reinterpret_cast<uintptr_t>(new_block)));
#endif

    return new_block;
}

auto Arena::blockIndex(const void* ptr) const -> size_t {
    return pointerOffset(ptr) / block_size(extract_power(reinterpret_cast<uintptr_t>(ptr)));
}

auto Arena::pointerOffset(const void* ptr) const -> intptr_t {
    return reinterpret_cast<intptr_t>(ptr) - reinterpret_cast<intptr_t>(base());
}

void Arena::setNext(const std::optional<Arena> arena) {
    header->next_arena = arena ? arena->base() : nullptr;
}

void Arena::setPrev(const std::optional<Arena> arena) {
    header->prev_arena = arena ? arena->base() : nullptr;
}

auto Arena::nextArena() const -> std::optional<Arena> {
    std::optional<Arena> arr =
        header->next_arena == nullptr ? std::nullopt : std::optional<Arena>(header->next_arena);
    return arr;
}

auto Arena::prevArena() const -> std::optional<Arena> {
    std::optional<Arena> arr =
        header->prev_arena == nullptr ? std::nullopt : std::optional<Arena>(header->prev_arena);
    return arr;
}

auto Arena::firstAllocatableBlockIndex() const -> size_t {
    return paddingBlocks();
}

auto Arena::lastAllocatableBlockIndex() const -> size_t {
    return firstAllocatableBlockIndex() + allocatableBlocks() - 1;
}

auto Arena::allocatableBlocks() const -> int32_t {
    return BLOCKS_PER_ARENA - 2 * paddingBlocks();
}

auto Arena::paddingBlocks() const -> int32_t {
    return std::max(1, static_cast<int32_t>(MIN_HEADER_SIZE / block_size(power())));
}

auto Arena::power() const -> uint8_t {
    return extract_power(reinterpret_cast<uintptr_t>(base()));
}

auto Arena::alignedToBlock(const void* ptr) const -> bool {
    return ((reinterpret_cast<uintptr_t>(ptr) >> power()) << power())
        == reinterpret_cast<uintptr_t>(ptr);
}

void Arena::free(const void* ptr) {
    if (ptr == nullptr) {
        return;
    }

    assert(contains(ptr));
    assert(alignedToBlock(ptr));

    // BUG: Also check whether `ptr` is pointing at the start of its block!
    Block* block = blockAt(blockIndex(ptr));
    block->next_block = header->free_list_head;
    header->free_list_head = block;
    header->free_count += 1;
}

void Arena::externalFree(const void* ptr) {
#if NOOBALLOC_METRICS
    ThreadAllocator::external_frees.fetch_add(1, std::memory_order_relaxed);
#endif

    if (ptr == nullptr) {
        return;
    }

    assert(contains(ptr));

    Block* block_of_ptr = blockAt(blockIndex(ptr));

    Block* thread_free_list_head;
    do {
        // Get the current head of the free list into a variable.
        thread_free_list_head = header->thread_free_list_head.load(std::memory_order::seq_cst);

        // Set that current head inside our block, which we currently still posses. The block is
        // still not part of of any free list.
        block_of_ptr->next_block = thread_free_list_head;

        // Check whether the head of the free list is still the same and set the address of our
        // block as the new head. If not still the same, try again from square one.
    } while (!header->thread_free_list_head.compare_exchange_strong(thread_free_list_head,
        block_of_ptr, std::memory_order::seq_cst, std::memory_order::seq_cst));
    header->thread_free_count.fetch_sub(1);
}

auto Arena::blockAt(size_t index) const -> Arena::Block* {
    assert(index >= firstAllocatableBlockIndex() && index <= lastAllocatableBlockIndex());
    return reinterpret_cast<Block*>(
        reinterpret_cast<uintptr_t>(base()) + index * block_size(power()));
}

auto Arena::allocateBlock() -> Block* {
    Block* allocated_block = header->free_list_head;
    if (allocated_block != nullptr) {
        Block* next_block = allocated_block->next_block;
        // If no next block, it will automatically be a `nullptr`.
        header->free_list_head = next_block;
        header->free_count -= 1;
    }
    return allocated_block;
}

auto Arena::contains(const void* ptr) const -> bool {
    return base() == arenaOf(ptr).base();
}

auto Arena::arenaOf(const void* ptr) -> Arena {
    ptr = noob_striptop(ptr);
    const uintptr_t _ptr = clear_inner_arena_info(reinterpret_cast<uintptr_t>(ptr));
    Arena arena = static_cast<Arena>(reinterpret_cast<void*>(_ptr));
    return arena;
}

void Arena::collectExternalFreeList() {
    assert(mine());

    // Collect the external free list.
    Block* external = header->thread_free_list_head.exchange(nullptr);
    if (external == nullptr) {
        // There were no blocks in the external free list.
        return;
    }

    // Count the number of blocks we just collected and find the tail of the list.
    uint16_t count = 1;
    Block* block = external;
    // We know there's at least one block, so safe to assume `block` is never `nullptr`.
    while (block->next_block != nullptr) {
        count += 1;
        block = block->next_block;
    }

    // Prepend the collected blocks to the local free list.
    Block* old_head = header->free_list_head;
    header->free_list_head = external;
    block->next_block = old_head;

    // Update both the local and external counts.
    header->free_count += count;
    header->thread_free_count.fetch_sub(count);
}
