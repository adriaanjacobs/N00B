#include "size_region_allocator.h"

#include <sys/mman.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <vector>

#include "NOOB/config.h"
#include "NOOB/memlayout.h"

SizeRegionAllocator::SizeRegionAllocator(uint8_t power)
    : power { power } { }

auto SizeRegionAllocator::reserve() -> void* {
    const std::lock_guard<std::mutex> guard(lock);

    void* new_free = getOrCreateFree();
    if (mprotect(new_free, allocationSize(), PROT_READ | PROT_WRITE) != 0) {
        perror("Failed to unprotect allocation");
        std::abort();
    }
    return new_free;
}

auto SizeRegionAllocator::getOrCreateFree() -> void* {
    void* existing = popExisting();
    if (existing != nullptr) {
        return existing;
    }

    return createNewFree();
}

auto SizeRegionAllocator::existingFree() const -> bool {
    return status_head != SIZE_MAX;
}

auto SizeRegionAllocator::popExisting() -> void* {
    if (!existingFree()) {
        return nullptr;
    }
    const size_t popped = status_head;
    status_head = status.at(popped);
    return allocationAt(popped);
}

auto SizeRegionAllocator::allocationAt(size_t index) const -> void* {
    return reinterpret_cast<void*>(size_region_base(power) + allocationSize() * index);
}

auto SizeRegionAllocator::allocationSize() const -> size_t {
    return std::max(4096UL, single_arena_size(power));
}

auto SizeRegionAllocator::createNewFree() -> void* {
    status.emplace_back(status.size());
    return allocationAt(status.size() - 1);
}

void SizeRegionAllocator::addToFreeList(size_t index) {
    assert(index < status.size());
    status.at(index) = status_head;
    status_head = index;
}

void SizeRegionAllocator::unreserve(void* space) {
    const std::lock_guard<std::mutex> guard(lock);

    const size_t index = indexOf(space);
    addToFreeList(index);

    if (mprotect(space, single_arena_size(power), PROT_READ) != 0) {
        perror("unreserve mprotect failed");
        std::abort();
    }
    // In multi-threaded environments, the allocator should never unmap pages as they could be
    // mapped again by the regular allocator. We therefore need to manually tell the kernel that the
    // pages are free again so it can recycle the underlying pages.
    madvise(space, single_arena_size(power), MADV_DONTNEED);
}

auto SizeRegionAllocator::indexOf(const void* ptr) const -> size_t {
    return (reinterpret_cast<uintptr_t>(ptr) - size_region_base(power)) / allocationSize();
}

auto SizeRegionAllocator::getPower() const -> uint8_t {
    return power;
}

auto SizeRegionAllocator::getInstance(uint8_t power) -> SizeRegionAllocator& {
    assert(power >= NOOB_MIN_POWER && power < NON_NOOB_MIN_POWER && "Power not correct");

    static std::deque<SizeRegionAllocator> allocators = []() {
        std::deque<SizeRegionAllocator> allocs;
        for (uint8_t i = NOOB_MIN_POWER; i < NON_NOOB_MIN_POWER; i++) {
            allocs.emplace_back(i);
        }
        return allocs;
    }();

    return allocators[power - NOOB_MIN_POWER];
}
