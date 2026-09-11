#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "NOOB/config.h"
#include "thread_size_allocator.h"

/**
 * When the N00B allocator is being loaded, its libc allocation API is already used before its
 * initialization code had a chance to run. Therefore use an indirection to set the value of
 * `hooked` and start with `false`. After the constructor code ran, it will set both this and the
 * value of `hooked` to `true`. Setting this to `true` ensures new threads will automatically hook
 * themselves. Setting `hooked` to `true` ensures the thread hooks itself as it will have
 * potentially already initialized its `hooked` using this variable during a call of the libc API.
 *
 * In other words, doing it like this ensures all allocations during initialization of the N00B
 * allocator are forwarded to the default libc allocator. After initialization, the N00B allocator
 * is hooked by the initialization code.
 */
extern bool hooked_init_value;

// Watch out with automatic static initialization. Because `hooked` is `true` by default,
// initialization code will already try to use our allocator, even though it's still initializing!
// Use patterns that avoid automatic static inialization.
/**
 * Whether the N00B allocator can handle a request (`hooked == true`) or whether the request will be
 * forwarded transparently to the libc allocator.
 */
extern thread_local bool hooked;

/**
 * RAII-style helper class to unhook the N00B allocator until the guard goes out of scope.
 */
class HookedGuard {
public:
    /**
     * Unhook the N00B allocator.
     */
    HookedGuard();

    /**
     * Restore the hook state before the creation of this `HookedGuard`. The allocator should still
     * be unhooked at this point.
     */
    ~HookedGuard();

private:
    /**
     * The hook state of the N00B allocator before the construction of the `HookedGuard` which will
     * be restored upon destruction.
     */
    bool old_value;
};

/**
 * Each thread gets its own thread-local allocator.
 */
class ThreadAllocator {
public:
#if NOOBALLOC_METRICS
    /**
     * Counter for number of allocators that are created.
     */
    inline static std::atomic_int32_t number_created = 0;

    /**
     * Number of allocations made by other threads freed by this one.
     */
    inline static std::atomic_int64_t external_frees = 0;
#endif

    /**
     * Allocate `nbytes` bytes using the current thread's allocator.
     */
    auto allocate(size_t nbytes) -> void*;

    /**
     * Free the given allocation. The `ptr` must be pointing to the base of the allocation.
     * Otherwise undefined behavior occurs.
     *
     * @param ptr The pointer to free.
     */
    void free(void* ptr);

    auto realloc(void* oldptr, size_t newsize) -> void*;

    auto zalloc(size_t nbytes) -> void*;

    /**
     * Get the single instance of this thread's allocator.
     *
     * @return The N00B allocator for the current thread.
     */
    static auto getInstance() -> ThreadAllocator&;

private:
    ThreadAllocator();
    ~ThreadAllocator();

    static constexpr const uint8_t min_power = std::max(4, NOOB_MIN_POWER);

    /**
     * Individual allocators for each size region.
     */
    std::vector<ThreadSizeAllocator> size_allocators;

#if NOOBALLOC_METRICS
    /**
     * Number of allocations on this allocator.
     */
    int64_t metric_allocations;

    /**
     * Number of frees with this allocator.
     */
    int64_t metric_frees;
#endif

    auto sizeAllocatorForBytes(size_t allocation_size) -> ThreadSizeAllocator&;

    auto sizeAllocatorForPower(uint8_t power) -> ThreadSizeAllocator&;
};
