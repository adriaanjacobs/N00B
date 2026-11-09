#include "thread_size_allocator.h"

#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>

#include "NOOB/config.h"
#include "NOOB/memlayout.h"
#include "arena.h"
#include "log.h"
#include "size_region_allocator.h"

ThreadSizeAllocator::ThreadSizeAllocator(uint8_t power)
    : power { power } { }

ThreadSizeAllocator::~ThreadSizeAllocator() {
    orphanArenas();
}

auto ThreadSizeAllocator::hasArenas() const -> bool {
    return first_arena.has_value();
}

auto ThreadSizeAllocator::allocate() -> void* {
    std::optional<Arena> arena = getOrCreateFreeArena();

    return arena ? arena->allocate() : nullptr;
}

void ThreadSizeAllocator::free(void* ptr) {
    Arena arena = Arena::arenaOf(ptr);

    if (arena.mine()) {
        arena.free(ptr);
        if (arena.isEmpty() && !hasSingleArena()) {
            // Just like mimalloc, we only free arenas during local frees. External frees may also
            // result in empty arenas, but those will either be reused for allocations by the local
            // thread or end up in the orphan arena list when the thread stops, which is fine.
            freeArena(arena);
        }
    } else {
        arena.externalFree(ptr);
    }
}

auto ThreadSizeAllocator::zalloc() -> void* {
    void* allocation = allocate();
    std::memset(allocation, 0, block_size(power));
    return allocation;
}

auto ThreadSizeAllocator::hasSingleArena() const -> bool {
    return hasArenas() && first_arena->base() == last_arena->base();
}

auto ThreadSizeAllocator::getFreeArena() -> std::optional<Arena> {
    std::optional<Arena> arena = last_free;
    if (arena && !arena->isFull()) {
        return arena;
    }

    while (arena && arena->isFull()) {
        arena = arena->nextArena();
    }

    if (!arena) {
        arena = first_arena;
        while (arena && arena->base() != last_free->base() && arena->isFull()) {
            arena = arena->nextArena();
        }
        if (arena->base() == last_free->base()) {
            arena = std::nullopt;
        }
    }

    if (arena) {
        last_free = arena;
    }

    return arena;
}

auto ThreadSizeAllocator::getOrCreateFreeArena() -> std::optional<Arena> {
    const std::optional<Arena> arena = getFreeArena();

    return arena ? arena : createArena();
}

void ThreadSizeAllocator::addArena(Arena& arena) {
    if (!hasArenas()) {
        first_arena = arena;
        last_arena = arena;
        last_free = arena;
    } else {
        last_arena->setNext(arena);
        arena.setPrev(last_arena);
        last_arena = arena;
    }
}

auto ThreadSizeAllocator::createArena() -> std::optional<Arena> {
    TRACE_FUNCTION();

    // Just try to recycle one each time we need to create an arena.
    // TODO: Try to recycle until no more available to recycle.
    std::optional<Arena> orphan = recycleOrphanArena();
    if (orphan) {
        addArena(*orphan);
        if (!orphan->isFull()) {
            return orphan;
        }
    }

    void* new_arena_base = SizeRegionAllocator::getInstance(power).reserve();
    if (new_arena_base == nullptr) {
        return std::nullopt;
    }
    Arena arena(new_arena_base);
    arena.initialize();
    addArena(arena);
    return arena;
}

void ThreadSizeAllocator::orphanArenas() {
    const std::lock_guard<std::mutex> guard(orphan_arenas_lock);
    std::optional<Arena> my_arena = first_arena;
    while (my_arena) {
        my_arena->orphan();
        my_arena = my_arena->nextArena();
    }
    std::optional<Arena> arena = orphan_arenas[power - NOOB_MIN_POWER];
    while (arena && arena->nextArena()) {
        arena = arena->nextArena();
    }
    if (arena && first_arena) {
        arena->setNext(first_arena);
        first_arena->setPrev(arena);
    } else if (first_arena) {
        orphan_arenas[power - NOOB_MIN_POWER] = first_arena;
    }
    first_arena = std::nullopt;
    last_arena = std::nullopt;
    last_free = std::nullopt;
}

auto ThreadSizeAllocator::recycleOrphanArena() const -> std::optional<Arena> {
    const std::lock_guard<std::mutex> guard(orphan_arenas_lock);
    std::optional<Arena> orphan = orphan_arenas[power - NOOB_MIN_POWER];
    if (orphan) {
        orphan_arenas[power - NOOB_MIN_POWER] = orphan->nextArena();
        orphan->setPrev(std::nullopt);
        orphan->setNext(std::nullopt);
        orphan->adopt();
    }
    return orphan;
}

void ThreadSizeAllocator::freeArena(Arena& arena) {
    TRACE_FUNCTION();
    std::optional<Arena> previous = arena.prevArena();
    std::optional<Arena> next = arena.nextArena();
    if (previous) {
        previous->setNext(next);
    } else {
        // Was the first arena.
        first_arena = next;
    }
    if (next) {
        next->setPrev(previous);
    } else {
        // Was the last arena.
        last_arena = previous;
    }
    if (arena.base() == last_free->base()) {
        last_free = first_arena;
    }
    arena.setNext(std::nullopt);
    arena.setPrev(std::nullopt);
    SizeRegionAllocator::getInstance(power).unreserve(arena.base());
}
