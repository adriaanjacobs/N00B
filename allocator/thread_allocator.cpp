#include "thread_allocator.h"

#include <bit>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "NOOB/config.h"
#include "NOOB/memlayout.h"
#include "log.h"
#include "noob_alloc.h"
#include "thread_size_allocator.h"

bool hooked_init_value = false;

thread_local bool hooked = hooked_init_value;

HookedGuard::HookedGuard()
    : old_value { hooked } {
    hooked = false;
}

HookedGuard::~HookedGuard() {
    assert(!hooked && "The N00B allocator should still be unhooked");
    hooked = old_value;
}

ThreadAllocator::ThreadAllocator() {
    // Called when a new thread uses the allocator for the first time (so basically "right after
    // thread startup").
    TRACE_FUNCTION();

    assert(!hooked && "The N00B allocator already hooked during initialization");

    for (uint8_t power = min_power; power < NON_NOOB_MIN_POWER; power++) {
        size_allocators.emplace_back(power);
    }

#if NOOBALLOC_METRICS
    number_created.fetch_add(1, std::memory_order_relaxed);
#endif
}

ThreadAllocator::~ThreadAllocator() {
    // Called when a thread exits.

    // Required to avoid recursion problems during deallocation.
    hooked = false;

#if NOOBALLOC_METRICS
    LOG("Thread metrics");
    LOG("\tAllocations: {}", metric_allocations);
    LOG("\tFrees: {}\t(of which external: {})", metric_frees,
        external_frees.load(std::memory_order_relaxed));
#endif
}

auto ThreadAllocator::allocate(size_t nbytes) -> void* {
    void* allocation = sizeAllocatorForBytes(nbytes).allocate();
#if NOOBALLOC_METRICS
    ++metric_allocations;
#endif
    return allocation;
}

void ThreadAllocator::free(void* ptr) {
    const uintptr_t ptrint = reinterpret_cast<uintptr_t>(ptr);

    assert(
        extract_offset(ptrint) == 0 && "The pointer is not pointing to the base of the allocation");
    // TODO: Find a better place to check this, so we can remove the N00B info from the pointer
    // earlier on and don't have to decide where to put that logic inside of the allocator. The
    // allocator class should not have to worry about pointers containing a TOPTAG anymore.
#if NOOB_TAG_POINTERS
    // Quick bounds check using the IPTAG and TOPTAG.
    assert(extract_inpointertag(ptrint) == extract_toptag(reinterpret_cast<uintptr_t>(ptr)));
#endif

    ptr = noob_striptop(ptr);
    const uint8_t power = extract_power(ptrint);
    sizeAllocatorForPower(power).free(ptr);
#if NOOBALLOC_METRICS
    ++metric_frees;
#endif
}

auto ThreadAllocator::realloc(void* oldptr, size_t newsize) -> void* {
    auto oldsize = block_size(extract_power(reinterpret_cast<uintptr_t>(oldptr)));
    if (oldsize >= newsize) {
        return oldptr;
    }
    void* newptr = allocate(newsize);
    std::memcpy(noob_striptop(newptr), noob_striptop(oldptr), oldsize);
    free(oldptr);
    return newptr;
}

auto ThreadAllocator::zalloc(size_t nbytes) -> void* {
    return sizeAllocatorForBytes(nbytes).zalloc();
}

auto ThreadAllocator::sizeAllocatorForBytes(size_t allocation_size) -> ThreadSizeAllocator& {
    if (allocation_size < 16) {
        allocation_size = 16;
    }
    return sizeAllocatorForPower(std::countr_zero(std::bit_ceil(allocation_size)));
}

auto ThreadAllocator::sizeAllocatorForPower(uint8_t power) -> ThreadSizeAllocator& {
    const size_t index = power - min_power;
    ThreadSizeAllocator& alloc = size_allocators.at(index);
    return alloc;
}

auto ThreadAllocator::getInstance() -> ThreadAllocator& {
    thread_local ThreadAllocator instance;
    return instance;
}
