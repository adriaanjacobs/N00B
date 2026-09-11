/**
 * @file noob_alloc.cpp
 *
 * Specialized API to the N00B allocator, which can be used explicitly by programs when they know
 * they are using the N00B allocator.
 */

#include "noob_alloc.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "NOOB/config.h"
#include "NOOB/memlayout.h"
#include "log.h"
#include "thread_allocator.h"
#include "util.h"

/**
 * Strip the TOPTAG from `ptr`. Some applications exceptionally require this functionality. This is
 * not a security hole as the TOPTAG still has to match the IPTAG during dereference.
 *
 * @param ptr The pointer to strip the TOPTAG of.
 * @return The `ptr` with its TOPTAG cleared.
 */
extern "C" auto noob_striptop(const void* ptr) -> const void* {
    return noob_striptop<const void>(ptr);
}

auto noob_malloc(size_t nbytes) -> void* {
    return ThreadAllocator::getInstance().allocate(nbytes + NOOBALLOC_PADDING_BYTES);
}

void noob_free(void* ptr) {
    if (ptr == nullptr) {
        return;
    }
    ThreadAllocator::getInstance().free(ptr);
}

auto noob_realloc(void* oldptr, size_t newsize) -> void* {
    if (oldptr == nullptr) {
        return ThreadAllocator::getInstance().allocate(newsize + NOOBALLOC_PADDING_BYTES);
    }
    if (newsize == 0) {
        ThreadAllocator::getInstance().free(oldptr);
        return nullptr;
    }
    return ThreadAllocator::getInstance().realloc(oldptr, newsize + NOOBALLOC_PADDING_BYTES);
}

auto noob_memalign(size_t alignment, size_t size) -> void* {
    assert(std::popcount(alignment) == 1 && "Alignment should be power of 2");

    if (size == 0) {
        return nullptr;
    }
    size = std::max(alignment, std::bit_ceil(size));
    return ThreadAllocator::getInstance().allocate(size + NOOBALLOC_PADDING_BYTES);
}

auto noob_calloc(size_t nmemb, size_t size) -> void* {
    return ThreadAllocator::getInstance().zalloc(nmemb * size + NOOBALLOC_PADDING_BYTES);
}

auto noob_malloc_usable_size(void* ptr) -> size_t {
    return block_size(extract_power(reinterpret_cast<uintptr_t>(ptr)));
}

/**
 * If `ptr` is a N00B pointer, check whether it is within bounds. If a check fails, the process
 * aborts. If `ptr` is not a N00B pointer, it is not checked.
 *
 * @param ptr The N00B pointer to check the bounds of.
 * @param base The corresponding base pointer of `ptr`.
 * @param check_deref Whether to perform a dereference check.
 * @param check_arith Whether to perform an arithmetic check.
 */
extern "C" void noob_access_check(void* ptr, void* base, bool check_deref, bool check_arith) {
    if (check_arith && !noob_ptr_arithmetic_valid(ptr, base)) {
        fprintf(stderr, "\n\nOut of bounds arithmetic detected!!\n");
        noob_print_ptr("ptr", ptr);
        noob_print_ptr("base", base);
        std::abort();
    }

    const uintptr_t baseint = reinterpret_cast<uintptr_t>(base);
    const uintptr_t ptrint = reinterpret_cast<uintptr_t>(ptr);
    const uint8_t power = extract_power(baseint);
    const uint16_t embedded_tag = extract_inpointertag(ptrint);
    const uint16_t top_tag = extract_toptag(ptrint);

    if (noob_is_nonnoob(baseint)) {
        return;
    }

    if (!check_deref) {
        return;
    }

    if (embedded_tag != top_tag) {
        fprintf(stderr, "\n\nDereference check failed!!\n");
        noob_print_ptr("ptr", ptr);

        if (base != ptr) {
            noob_print_ptr("base", base);
        }

        if (noob_is_nonnoob(ptrint) && noob_is_nonnoob(baseint)) {
            std::abort();
        }

        const uintptr_t likely_base = noob_embed_inpointer_tag(ptrint, top_tag);
        fprintf(stderr, "likely OOB offset %ld into object at %p (size %zu)\n",
            ptrint - likely_base, noob_striptop(reinterpret_cast<void*>(likely_base)),
            block_size(power));

        fprintf(stderr, "embedded_tag: %u\n", embedded_tag);
        fprintf(stderr, "top_tag: %u\n", top_tag);
        std::abort();
    }

    // check if they're accessible
    volatile char* accessptr = static_cast<volatile char*>(ptr);
    *accessptr;
}
