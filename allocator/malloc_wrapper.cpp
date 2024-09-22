#include "nooballoc.h"

#include <malloc.h>
#include <stdlib.h>
#include <assert.h>

#include <iterator>
#include <bit>
#include <functional>
#include <type_traits>
#include <iostream>

bool hooked = false;

struct unhook_scope {
    const bool oldval;

    unhook_scope () :
        oldval{hooked}
    {
        hooked = false;
    }

    ~unhook_scope () {
        assert(!hooked);
        hooked = oldval;
    }
};

[[gnu::constructor(0)]]
void init_noob() {
    noob_init(42 - TAG_WIDTH - 1, &hooked);
    hooked = true;
}

#define IF_NOT_HOOKED(expr)    \
    if (!hooked)                \
        return expr;            \
    unhook_scope guard{};

template<typename Func, typename... Args>
std::invoke_result_t<Func, Args...> 
logged_call(const char* funcname, Func&& f, Args&&... args) {
    std::cerr << funcname << "(";
    ((std::cerr << std::forward<Args>(args) << ","), ...);
    std::cerr << ")";
    // handle void-returning functions
    if constexpr (std::is_same_v<std::invoke_result_t<Func, Args...>, void>) {
        std::cerr << "\n";
        return std::invoke(std::forward<Func>(f), std::forward<Args>(args)...);
    } else {
        auto&& result = std::invoke(std::forward<Func>(f), std::forward<Args>(args)...);
        std::cerr << " -> " << result << "\n";
        return result;
    }
}

#define DO_LOGGING 0

#if DO_LOGGING
#define LOGGED_CALL(func, ...) logged_call(#func, func, __VA_ARGS__)
#else
#define LOGGED_CALL(func, ...) func(__VA_ARGS__)
#endif

extern "C" {

decltype(malloc) __libc_malloc;
void* malloc(size_t nbytes) {
    IF_NOT_HOOKED(__libc_malloc(nbytes));
    return LOGGED_CALL(noob_malloc, nbytes);
}

decltype(free) __libc_free;
void free(void* ptr) {
    IF_NOT_HOOKED(__libc_free(ptr));
    return LOGGED_CALL(noob_free, ptr);
}

decltype(realloc) __libc_realloc;
void* realloc(void* oldptr, size_t newsize) {
    IF_NOT_HOOKED(__libc_realloc(oldptr, newsize));
    return LOGGED_CALL(noob_realloc, oldptr, newsize);
}

void* reallocarray (void *ptr, size_t nmemb, size_t size) {
    return realloc(ptr, nmemb * size);
}

decltype(memalign) __libc_memalign;
void* memalign(size_t alignment, size_t size) {
    IF_NOT_HOOKED(__libc_memalign(alignment, size));
    return LOGGED_CALL(noob_memalign, alignment, size);
}

void* valloc (size_t size) {
    return memalign(0x1000, size);
}

void* aligned_alloc (size_t alignment, size_t size) {
    return memalign(alignment, size);
}

int posix_memalign (void **memptr, size_t alignment, size_t size) {
    if (alignment % sizeof(void*) != 0 || std::popcount(alignment) != 1)
        return EINVAL;
    *memptr = memalign(alignment, size);
    return 0;
}

decltype(calloc) __libc_calloc;
void* calloc(size_t nmemb, size_t size) {
    IF_NOT_HOOKED(__libc_calloc(nmemb, size));
    return LOGGED_CALL(noob_calloc, nmemb * size);
}

}
