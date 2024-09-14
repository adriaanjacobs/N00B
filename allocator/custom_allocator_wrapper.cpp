#include "nooballoc.h"

#include <stdlib.h>
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

#include <optional>
#include <iterator>

#define DLSYM(handle, funcname) ({                                                                  \
    dlerror(); /* clear dlsym errors  */                                                            \
    decltype(funcname)* _func = reinterpret_cast<decltype(funcname)*>(dlsym(handle, #funcname));    \
    if (const char* error = dlerror()) {                                                            \
        fprintf(stderr, "dlsym: %s\n", error);                                                      \
        exit(-1);                                                                                   \
    }                                                                                               \
    _func;                                                                                          \
})

// keep everything default while we are still setting up the lib
// malloc
extern "C" decltype(malloc) __libc_malloc;
decltype(malloc)* custom_malloc_fn = __libc_malloc;
// free
extern "C" decltype(free) __libc_free;
decltype(free)* custom_free_fn = __libc_free;
// realloc
extern "C" decltype(realloc) __libc_realloc;
decltype(realloc)* custom_realloc_fn = __libc_realloc;
// FIXME: add malloc_usable_size, memalign, strdup (?), 

[[gnu::constructor(0)]]
void switch_to_custom_allocator() {
    auto library_path = getenv("LIBALLOCATOR");
    if (!library_path) {
        fprintf(stderr, "'LIBALLOCATOR' not specified: Please set the 'LIBALLOCATOR' env var to the path of the allocator library\n");
        exit(-1);
    }

    dlerror();
    auto handle = dlmopen(LM_ID_NEWLM, library_path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        assert(!handle);
        fprintf(stderr, "Failed to open %s\n", dlerror());
        exit(-1);
    }

    // lookup init function & invoke
    auto noob_init_fn = DLSYM(handle, noob_init);
    noob_init_fn(42 - TAG_WIDTH - 1);

    // re-point the custom allocator functions now
    custom_malloc_fn = DLSYM(handle, noob_malloc);
    custom_free_fn = DLSYM(handle, noob_free);
    custom_realloc_fn = DLSYM(handle, noob_realloc);
}

void non_allocating_printf(const char* fmt, ...) {
    va_list vargs;
    va_start(vargs, fmt);
    char buf[0x1000];
    snprintf(buf, std::size(buf), fmt, vargs);
    write(STDERR_FILENO, buf, strlen(buf) + 1);
    va_end(vargs);
}

extern "C" {

void* malloc(size_t bytes) {
    non_allocating_printf("malloc(%lu)\n", bytes);
    return custom_malloc_fn(bytes);
}

void free(void* ptr) {
    non_allocating_printf("free(%p)\n", ptr);
    return custom_free_fn(ptr);
}

void* realloc(void* oldptr, size_t newsize) {
    non_allocating_printf("realloc(%p, %lu)\n", oldptr, newsize);
    return custom_realloc_fn(oldptr, newsize);
}

}
