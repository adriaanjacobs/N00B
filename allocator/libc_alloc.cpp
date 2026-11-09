/**
 * @file libc_alloc.cpp
 *
 * The libc allocation interface for the N00B allocator. The interface is meant to override the
 * default one provided by the system's libc implementation and ensures the N00B allocator is a
 * drop-in replacement. Since the N00B allocator also uses this interface internally, it should also
 * prevent recursive calls and instead forward internal calls to the regular libc allocator.
 */

#include <malloc.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>

#include "log.h"
#include "noob_alloc.h"
#include "thread_allocator.h"
#include "util.h"

// We need to create a function that lazily loads the real `malloc_usable_size()` function from
// glibc. Unlike the other functions which we wrap, we can't use the same trick as a "private"
// equivalent function with a different name doesn't exist in glibc.
LAZY_DLSYM_FN(malloc_usable_size);

// clang-tidy seems to have problems with this way of defining the functions as we're not including
// any headers for them.
// NOLINTBEGIN(misc-include-cleaner)

// We use the weird way to declare that the private functions exist, but there doesn't seem to be a
// way to use trailing return types for that, even though clang-tidy suggests to use them.
// NOLINTBEGIN(modernize-use-trailing-return-type)

// Glibc declares the following functions internally without exposing them in its headers. They have
// the exact same behavior as their public counterparts. Declare them here so we can use them if we
// need to use the glibc allocator since our allocator overwrites the public allocation functions.
extern "C" decltype(malloc) __libc_malloc;
extern "C" decltype(calloc) __libc_calloc;
extern "C" decltype(realloc) __libc_realloc;
extern "C" decltype(free) __libc_free;
extern "C" decltype(memalign) __libc_memalign;
// NOLINTEND(modernize-use-trailing-return-type)

////////////////////////////////////////////////////////////////////////////////////////////////////
/// Direct libc allocation functions
////////////////////////////////////////////////////////////////////////////////////////////////////

__attribute__((visibility("default"))) extern "C" auto malloc(size_t size) -> void* {
    if (!hooked) {
        return __libc_malloc(size);
    }
    const HookedGuard guard;
    return TRACED_CALL(noob_malloc, size);
}

__attribute__((visibility("default"))) extern "C" auto calloc(size_t nmemb, size_t size) -> void* {
    if (!hooked) {
        return __libc_calloc(nmemb, size);
    }
    const HookedGuard guard;
    return TRACED_CALL(noob_calloc, nmemb, size);
}

__attribute__((visibility("default"))) extern "C" auto realloc(void* ptr, size_t size) -> void* {
    if (!is_noob_allocation(ptr)) {
        return __libc_realloc(ptr, size);
    }
    if (hooked) {
        const HookedGuard guard;
        return TRACED_CALL(noob_realloc, ptr, size);
    }

    return nullptr;
}

__attribute__((visibility("default"))) extern "C" void free(void* ptr) {
    if (!is_noob_allocation(ptr)) {
        __libc_free(ptr);
    } else if (hooked) {
        const HookedGuard guard;
        TRACED_CALL(noob_free, ptr);
    }
}

// Deprecated in glibc allocator, but some programs still use it.
__attribute__((visibility("default"))) extern "C" auto memalign(size_t alignment,
    size_t size) -> void* {
    if (!hooked) {
        return __libc_memalign(alignment, size);
    }
    const HookedGuard guard;
    return TRACED_CALL(noob_memalign, alignment, size);
}

__attribute__((visibility("default"))) extern "C" auto malloc_usable_size(void* ptr) -> size_t {
    if (!hooked) {
        return malloc_usable_size_fn()(ptr);
    }
    const HookedGuard guard;
    return TRACED_CALL(noob_malloc_usable_size, ptr);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Libc allocation "aliases" that forward to real libc allocation functions
////////////////////////////////////////////////////////////////////////////////////////////////////

__attribute__((visibility("default"))) extern "C" auto reallocarray(void* ptr, size_t nmemb,
    size_t size) -> void* {
    return realloc(ptr, (nmemb * size));
}

__attribute__((visibility("default"))) extern "C" auto posix_memalign(void** memptr,
    size_t alignment, size_t size) -> int {
    if (alignment % sizeof(void*) != 0 || std::popcount(alignment) != 1) {
        return EINVAL;
    }
    *memptr = memalign(alignment, size);
    return 0;
}

__attribute__((visibility("default"))) extern "C" auto aligned_alloc(size_t alignment,
    size_t size) -> void* {
    return memalign(alignment, size);
}

// Deprecated in glibc allocator, but some programs still use it.
__attribute__((visibility("default"))) extern "C" auto valloc(size_t size) -> void* {
    return memalign(sysconf(_SC_PAGESIZE), size);
}

// NOLINTEND(misc-include-cleaner)
