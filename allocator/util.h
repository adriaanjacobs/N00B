/**
 * @file util.h
 *
 * Small utility macros and functions used in the N00B allocator.
 */

#pragma once

#include <dlfcn.h>

#include <cstdint>

#include "arena.h"

/**
 * Dynamically search for the function with the given name using the given search strategy.
 *
 * @param handle One of the `RTLD_*` strategies (see `dlsym()` documentation).
 * @param funcname The name of the symbol to try to find.
 * @return Function pointer if the function is found.
 */
#define DLSYM(handle, funcname)                                                                    \
    ({                                                                                             \
        dlerror(); /* clear dlsym errors  */                                                       \
        decltype(funcname)* _func =                                                                \
            reinterpret_cast<decltype(funcname)*>(dlsym(handle, #funcname));                       \
        if (const char* error = dlerror()) {                                                       \
            fprintf(stderr, "dlsym: %s\n", error);                                                 \
            exit(-1);                                                                              \
        }                                                                                          \
        _func;                                                                                     \
    })

/**
 * Obtain a pointer to the next occurrence of the symbol with the given name in the search order.
 * This is stored after first use so next calls don't have to find it again.
 *
 * @param func The name of the symbol (function) to look for.
 * @return A function that returns the next occurrence of the symbol with the given name.
 */
#define LAZY_DLSYM_FN(func)                                                                        \
    namespace {                                                                                    \
    auto func##_fn() -> decltype(func)* {                                                          \
        static auto function = DLSYM(RTLD_NEXT, func);                                             \
        return function;                                                                           \
    }                                                                                              \
    }

/**
 * Assert whether `cond` holds and if not, abort the process with an error message containing info
 * about the failing source line.
 *
 * @param cond The condition to assert.
 */
#define ASSERT_ELSE_PERROR(cond)                                                                   \
    do {                                                                                           \
        bool x = static_cast<bool>(cond);                                                          \
        if (!x) {                                                                                  \
            fflush(stdout);                                                                        \
            fflush(stderr);                                                                        \
            fprintf(stderr, "%s:%d: %s: Assertion `%s` failed (errno=%d): ", __FILE__, __LINE__,   \
                __func__, #cond, errno);                                                           \
            perror("");                                                                            \
            fflush(stderr);                                                                        \
            abort();                                                                               \
        }                                                                                          \
    } while (false)

/**
 * The maximum possible value for the TOPTAG of a N00B pointer.
 */
constexpr unsigned long TOPTAG_MAX = Arena::BLOCKS_PER_ARENA - 1UL;

/**
 * Check whether the allocation pointed to by `ptr` is not checked by N00B.
 *
 * @param ptr The pointer to check.
 * @return `true` if the allocation is not checked by N00B.
 */
auto noob_is_nonnoob(uintptr_t ptr) -> bool;

/**
 * Set the IPTAG of N00B pointer `ptr` to `tag`. The `tag` must fit inside the IPTAG field.
 *
 * @param ptr The N00B pointer to set the IPTAG of.
 * @param tag The IPTAG to set in the pointer.
 * @return The pointer with the new `tag` set as IPTAG.
 */
auto noob_embed_inpointer_tag(uintptr_t ptr, uint16_t tag) -> uintptr_t;

/**
 * Set the TOPTAG of N00B pointer `ptr` to `tag`. The `tag` must fit inside the TOPTAG field.
 *
 * @param ptr The N00B pointer to set the TOPTAG of.
 * @param tag The TOPTAG to set in the pointer.
 * @return The pointer with the new `tag` set as TOPTAG.
 */
auto noob_embed_top_tag(uintptr_t ptr, uint16_t tag) -> uintptr_t;

/**
 * Set the TOPTAG of N00B pointer `ptr` equal to its IPTAG.
 *
 * @param ptr The N00B pointer to set the TOPTAG of.
 * @return The pointer with its current IPTAG set as its TOPTAG.
 */
auto noob_embed_top_tag(uintptr_t ptr) -> uintptr_t;

/**
 * Given a N00B pointer and its base pointer (the copy of that pointer at the point when the pointer
 * was created), check whether all pointer arithmetic operations performed on the pointer resulted
 * in a pointer that is still within bounds.
 *
 * @param ptr The N00B pointer to check.
 * @param base The corresponding base pointer.
 * @return Whether the pointer is within bounds according to the arithmetic check.
 */
auto noob_ptr_arithmetic_valid(const void* ptr, const void* base) -> bool;

#if __aarch64__
/**
 * Try to enable TBI on AArch64 architectures for the process. This will return whether the
 * extension was enabled.
 *
 * @return Whether the extension was enabled for the current thread.
 */
auto enable_tbi() -> bool;
#endif

#if __x86_64__
/**
 * Try to enable the LAM extension on x86-64 architectures for the process. This will return whether
 * the extension was enabled. This may exit the process if a serious error occurs.
 *
 * @return Whether the extension was enabled for the current thread.
 */
auto enable_lam() -> bool;
#endif

/**
 * Validate whether the N00B memory region is correctly initialized. It's considered correctly
 * initialized when the entire virtual memory region starting from the lowest allowed address up to
 * the address chosen by `NON_NOOB_MIN_POWER` is reserved and a value in the auxiliary vector
 * indicates that the N00B Loader ran correctly.
 *
 * @return Whether the virtual memory of the process is correctly set up for N00B.
 */
auto noob_region_initialized() -> bool;
