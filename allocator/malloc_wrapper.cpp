#include "nooballoc.h"

#include <malloc.h>
#include <stdlib.h>
#include <assert.h>

#include <iterator>
#include <bit>

bool initialized = false;
bool inside_noob = false;

struct set_inside_noob {
    const bool oldval;

    set_inside_noob () :
        oldval{inside_noob}
    {
        inside_noob = true;
    }

    ~set_inside_noob () {
        assert(inside_noob);
        inside_noob = oldval;
    }
};

[[gnu::constructor(0)]]
void init_noob() {
    set_inside_noob guard{};
    noob_init(42 - TAG_WIDTH - 1, &inside_noob);
    initialized = true;
}

#define IF_NOT_INSIDE_NOOB(expr)        \
    if (inside_noob || !initialized)    \
        return expr;                    \
    set_inside_noob guard{};

extern "C" {

decltype(malloc) __libc_malloc;
void* malloc(size_t nbytes) {
    IF_NOT_INSIDE_NOOB(__libc_malloc(nbytes));
    return noob_malloc(nbytes);
}

decltype(free) __libc_free;
void free(void* ptr) {
    IF_NOT_INSIDE_NOOB(__libc_free(ptr));
    return noob_free(ptr);
}

decltype(realloc) __libc_realloc;
void* realloc(void* oldptr, size_t newsize) {
    IF_NOT_INSIDE_NOOB(__libc_realloc(oldptr, newsize));
    return noob_realloc(oldptr, newsize);
}

void* reallocarray (void *ptr, size_t nmemb, size_t size) {
    return realloc(ptr, nmemb * size);
}

decltype(memalign) __libc_memalign;
void* memalign(size_t alignment, size_t size) {
    IF_NOT_INSIDE_NOOB(__libc_memalign(alignment, size));
    return noob_memalign(alignment, size);
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
    IF_NOT_INSIDE_NOOB(__libc_calloc(nmemb, size));
    return noob_calloc(nmemb * size);
}

}
