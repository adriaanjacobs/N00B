#include "nooballoc.h"

#include <NOOB/config.h>

#include <malloc.h>
#include <stdlib.h>
#include <assert.h>
#include <dlfcn.h>

#include <bit>
#include <functional>
#include <type_traits>
#include <iostream>

#define DLSYM(handle, funcname) ({                                                                  \
    dlerror(); /* clear dlsym errors  */                                                            \
    decltype(funcname)* _func = reinterpret_cast<decltype(funcname)*>(dlsym(handle, #funcname));    \
    if (const char* error = dlerror()) {                                                            \
        fprintf(stderr, "dlsym: %s\n", error);                                                      \
        exit(-1);                                                                                   \
    }                                                                                               \
    _func;                                                                                          \
})

#define LAZY_DLSYM_FN(func)                         \
    static decltype(func)* func##_fn () {           \
        static auto fn = DLSYM(RTLD_NEXT, func);    \
        return fn;                                  \
    }

LAZY_DLSYM_FN(malloc_usable_size);

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

// for use by applications that (exceptionally) need to strip top bits
//  not a security hole -> top bits still have to match in-pointer bits during deref
//  helpful to avoid adversarial edge cases, e.g., gcc's GGC implementation
const void* noob_striptop(const void* ptr) {
    // force the c++ version
    return noob_striptop<const void>(ptr);
}

decltype(malloc) __libc_malloc;
void* malloc(size_t nbytes) {
    IF_NOT_HOOKED(__libc_malloc(nbytes));
    return LOGGED_CALL(noob_malloc, nbytes + PAD_BY_ONE_BYTE);
}

decltype(free) __libc_free;
void free(void* ptr) {
    IF_NOT_HOOKED(__libc_free(ptr));
    return LOGGED_CALL(noob_free, ptr);
}

decltype(realloc) __libc_realloc;
void* realloc(void* oldptr, size_t newsize) {
    IF_NOT_HOOKED(__libc_realloc(oldptr, newsize));
    return LOGGED_CALL(noob_realloc, oldptr, newsize + PAD_BY_ONE_BYTE);
}

void* reallocarray (void *ptr, size_t nmemb, size_t size) {
    return realloc(ptr, (nmemb * size) + PAD_BY_ONE_BYTE);
}

decltype(memalign) __libc_memalign;
void* memalign(size_t alignment, size_t size) {
    IF_NOT_HOOKED(__libc_memalign(alignment, size));
    return LOGGED_CALL(noob_memalign, alignment, size + PAD_BY_ONE_BYTE);
}

void* valloc (size_t size) {
    return memalign(0x1000, size + PAD_BY_ONE_BYTE);
}

void* aligned_alloc (size_t alignment, size_t size) {
    return memalign(alignment, size + PAD_BY_ONE_BYTE);
}

int posix_memalign (void **memptr, size_t alignment, size_t size) {
    if (alignment % sizeof(void*) != 0 || std::popcount(alignment) != 1)
        return EINVAL;
    *memptr = memalign(alignment, size + PAD_BY_ONE_BYTE);
    return 0;
}

decltype(calloc) __libc_calloc;
void* calloc(size_t nmemb, size_t size) {
    IF_NOT_HOOKED(__libc_calloc(nmemb, size));
    return LOGGED_CALL(noob_calloc, (nmemb * size) + PAD_BY_ONE_BYTE);
}

size_t malloc_usable_size(void* ptr) {
    IF_NOT_HOOKED(malloc_usable_size_fn()(ptr));
    return LOGGED_CALL(noob_usable_size, ptr);
}

}
