/**
 * @file init.cpp
 *
 * Allocator initialization/cleanup procedures. Initialization has to ensure the allocator gets
 * hooked, while cleanup procedures need to unhook before running.
 */

#include <cstdlib>
#include <iostream>

#include "NOOB/config.h"
#include "log.h"
#include "thread_allocator.h"
#include "util.h"

__attribute__((constructor(101))) void nooballoc_constructor() {
    if (std::getenv("NOOBALLOC_LOG") != nullptr) {
        nooballoc::log::logging_enabled = true;
    }

    TRACE_FUNCTION();

    if (!noob_region_initialized()) {
        std::cerr << "N00B memory not initialized correctly. Are you using the N00B Loader?\n";
        std::exit(EXIT_FAILURE);
    }

#if NOOB_TAG_POINTERS
#if __aarch64__
    if (!enable_tbi()) {
        std::cerr << "Failed to enable ARM TBI!\n";
        std::abort();
    }
#elif __x86_64__
    if (!enable_lam()) {
        std::cerr << "Failed to enable LAM!\n";
        std::abort();
    }
#else
#error "Pointer tagging enabled but not implemented for this architecture"
#endif
#endif

    LOG("N00B allocator initialized");

    // This ensures new threads will automatically hook themselves.
    hooked_init_value = true;
    // Our own thread's hooked variable might have already been initialized from the value of
    // `hooked_init_value`, so we need to explicitly hook now that all initialization logic has had
    // a chance to run.
    hooked = true;
}

__attribute__((destructor(101))) void nooballoc_destructor() {
    const HookedGuard guard;

    TRACE_FUNCTION();

#if NOOBALLOC_METRICS
    LOG("{} allocators created", ThreadAllocator::number_created.load(std::memory_order_relaxed));
#endif
    LOG("N00B allocator deinitialized");
}
