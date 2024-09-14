#include "nooballoc.h"

#include <stdlib.h>
#include <assert.h>

#include <iterator>

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

}
