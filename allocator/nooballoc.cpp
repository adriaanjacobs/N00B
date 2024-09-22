#include "nooballoc.h"

#include <errno.h>
#include <stddef.h>
#include <assert.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

#include <vector>
#include <bit>
#include <optional>
#include <bitset>

#define NUM_BLOCKS_IN_ARENA (1ULL << TAG_WIDTH)

#define ASSERT_ELSE_PERROR(cond) \
    do {                            \
        bool x = static_cast<bool>(cond);                \
        if (!x) {                       \
            fflush(stdout);                 \
            fflush(stderr);                     \
            fprintf(stderr,"%s:%d: %s: Assertion `%s` failed: ", __FILE__, __LINE__, __func__, #cond); \
            perror("");                                                                         \
            fflush(stderr);                                                                     \
            abort();                                                                            \
        }                                                                                       \
    } while (false)

uintptr_t size_region_base(size_t radix) { 
    return radix << 42; 
}

size_t size_region_size() {
    return (1ULL << 42); 
}

size_t block_size(size_t radix) {
    return (1ULL << radix);
}

size_t extract_radix(uintptr_t ptr) {
    return (ptr >> 42) & 0b0011'1111;
}

size_t extract_lowestMSBs(uintptr_t ptr) {
    auto radix = extract_radix(ptr);
    return (ptr >> radix) & (~0ULL >> (64 - TAG_WIDTH));
}

size_t extract_topbits(uintptr_t ptr) {
    return ptr >> (64 - TAG_WIDTH);
}

size_t extract_offset(uintptr_t ptr) {
    auto radix = extract_radix(ptr);
    auto mask = (1ULL << radix) - 1;
    return ptr & mask;
}

size_t extract_highestMSBs(uintptr_t ptr) {
    auto radix = extract_radix(ptr);
    ptr &= (~0ULL >> TAG_WIDTH); // clear the top bits
    ptr >>= radix + TAG_WIDTH; // shift away the offset + lowestMSBs
    return ptr;
}

template<typename T>
T* noob_striptop(T* ptr) {
    return (T*) ((uintptr_t) ptr & (~0ULL >> TAG_WIDTH)); 
}

struct NOOBArena {
    const size_t radix;
    void* base;
    void* bottom_of_three_base;
    std::bitset<NUM_BLOCKS_IN_ARENA> free_status;
    std::bitset<NUM_BLOCKS_IN_ARENA> fresh_status;

    size_t single_arena_size() const {
        return NUM_BLOCKS_IN_ARENA * block_size(radix);
    }

    bool is_full() const {
        return free_status.none();
    }

    bool has_fresh_blocks() const {
        return fresh_status.any();
    }

    bool contains(void* ptr) {
        return extract_highestMSBs((uintptr_t) ptr) == extract_highestMSBs((uintptr_t) base);
    }

    NOOBArena(size_t radix, void* suggested_location) :
        radix{radix}
    {
        free_status.flip(); // set all to free
        fresh_status.flip(); // at first, all blocks are fresh
        auto aloc = (uintptr_t) suggested_location;
        while (true) { // round-robin try out all the possibilities
            // check if available
            auto possible_base = mmap64((void*) aloc, 3*single_arena_size(), PROT_NONE, MAP_ANON|MAP_PRIVATE|MAP_NORESERVE|MAP_FIXED_NOREPLACE, -1, 0);
            if (possible_base != MAP_FAILED)
                break;
            ASSERT_ELSE_PERROR(errno == EEXIST);

            // increment & mask
            aloc = aloc + ((aloc - size_region_base(radix)) % size_region_size());

            // if we've gone all the way around, then we couldn't find any possible mapping
            assert(aloc != (uintptr_t) suggested_location && "No more VM space available");
        }

        // we've found one at aloc
        bottom_of_three_base = (void*) aloc;
        base = (void*) (aloc + single_arena_size());
        // get rw access to the actual arena in the middle
        ASSERT_ELSE_PERROR(mprotect(base, single_arena_size(), PROT_READ|PROT_WRITE) == 0);
    }

    void* allocate(uint idx) {
        // use this one
        free_status[idx] = false;
        fresh_status[idx] = false;

        // calculate the address of the block at this idx
        auto ptr = ((uintptr_t) base) + idx * block_size(radix);
        assert(extract_radix(ptr) == radix);
        
        // embed the lowestMSBs in the top bits now
        auto mask = extract_lowestMSBs(ptr) << (64 - TAG_WIDTH);
        ptr ^= mask;
        return (void*) ptr;
    }

    void* allocate() {
        auto idx = free_status._Find_first();
        assert(idx < free_status.size());
        return allocate(idx);
    }

    void free(void* ptr) {
        auto lowestMSBs = extract_lowestMSBs((uintptr_t) ptr);
        auto idx = lowestMSBs - extract_lowestMSBs((uintptr_t) base);
        assert(idx < free_status.size());
        assert(free_status[idx] == false && "Double free! This block is already free.");
        free_status[idx] = true;
    }

    void* zalloc() {
        auto idx = fresh_status._Find_first();
        assert(idx < fresh_status.size());
        return allocate(idx);
    }
};

struct NOOBSizeAllocator {
    const size_t radix;
    std::vector<NOOBArena> arenas;

    NOOBSizeAllocator(size_t radix) : 
        radix{radix}
    {}

    void* figure_out_a_good_base_suggestion() {
        void* suggested_base = (void*) size_region_base(radix);
        for (auto& arena : arenas) 
            suggested_base = (void*) ((uintptr_t) arena.bottom_of_three_base + 3*arena.single_arena_size());

        if (extract_radix((uintptr_t) suggested_base) != radix)
            suggested_base = (void*) size_region_base(radix);
        return suggested_base;
    }

    NOOBArena& get_or_create_arena() {
        for (auto& arena : arenas) {
            if (arena.is_full())
                continue;
            
            return arena;
        }

        return arenas.emplace_back(radix, figure_out_a_good_base_suggestion());
    }

    void* allocate() {
        auto& arena = get_or_create_arena();
        auto ret = arena.allocate();
        assert(ret);
        return ret;
    }

    void free(void* ptr) {
        for (auto& arena : arenas) {
            if (arena.contains(ptr))
                return arena.free(ptr);
        }
        assert(!"Double free? Allocator does not contain `ptr`");
    }

    void* zalloc() {
        NOOBArena* arena_with_free_space = nullptr;
        for (auto& arena : arenas) {
            if (!arena.is_full())
                arena_with_free_space = &arena;
            if (arena.has_fresh_blocks())
                return arena.zalloc();
        }

        // no arenas with fresh blocks, memzero some existing block
        if (arena_with_free_space) {
            auto block = arena_with_free_space->allocate();
            memset(noob_striptop(block), 0, block_size(radix));
        }

        // not even an arena with free blocks at all, find a new one
        return arenas.emplace_back(radix, figure_out_a_good_base_suggestion()).allocate();
    }
};

struct NOOBAllocator {
    // determines maximum allocation size
    const size_t max_radix;
    const size_t min_radix = 4;
    bool* const hooked;

    std::vector<NOOBSizeAllocator> per_size_allocators;

    NOOBAllocator(size_t max_radix, bool* hooked) :
        max_radix{max_radix},
        hooked{hooked}
    {
        assert(max_radix > min_radix && max_radix < (42 - TAG_WIDTH));
        for (uint radix = min_radix; radix <= max_radix; radix++) {
            per_size_allocators.push_back(NOOBSizeAllocator{radix});
        }
    }

    ~NOOBAllocator() {
        // ensure that our deallocation will not trigger a bunch of recursive `free` invocations
        *hooked = false;
    }

    NOOBSizeAllocator& getSizeAllocatorForRadix(size_t radix) {
        size_t index = radix - min_radix;
        return per_size_allocators.at(index);
    }

    NOOBSizeAllocator& getSizeAllocatorFor(size_t allocation_size) {
        if (allocation_size < 16)
            allocation_size = 16;
        return getSizeAllocatorForRadix(std::countr_zero(std::bit_ceil(allocation_size)));
    }

    void* allocate(size_t nbytes) {
        return getSizeAllocatorFor(nbytes).allocate();
    }

    void free(void* ptr) {
        auto radix = extract_radix((uintptr_t) ptr);
        // now for a quick security check
        // check that it is still pointing to the original alloc
        assert(extract_lowestMSBs((uintptr_t) ptr) == extract_topbits((uintptr_t) ptr));
        // check that it is pointing to the base of the alloc
        assert(extract_offset((uintptr_t) ptr) == 0);

        // now delegate to the relevant size allocator to mark as free
        getSizeAllocatorForRadix(radix).free(ptr);
    }

    void* realloc(void* oldptr, size_t newsize) {
        auto oldsize = 1ULL << extract_radix((uintptr_t) oldptr);
        if (oldsize >= newsize)
            return oldptr;
        auto newptr = allocate(newsize);
        memcpy(noob_striptop(newptr), noob_striptop(oldptr), oldsize);
        free(oldptr);
        return newptr;
    }

    void* zalloc(size_t nbytes) {
        return getSizeAllocatorFor(nbytes).zalloc();
    }
};

void noob_non_allocating_printf(const char* fmt, ...) {
#if 0
    va_list vargs;
    va_start(vargs, fmt);
    char buf[0x1000];
    snprintf(buf, std::size(buf), fmt, vargs);
    write(STDERR_FILENO, buf, strlen(buf) + 1);
    va_end(vargs);
#endif
}

std::optional<NOOBAllocator> noob_allocator = std::nullopt;

void noob_init(size_t max_radix, bool* hooked) {
    assert(!noob_allocator.has_value() && "NOOB is already initialized!");
    noob_allocator.emplace(max_radix, hooked);
}

void* noob_malloc(size_t nbytes) {
    noob_non_allocating_printf("noob_malloc(%lu)\n", nbytes);
    assert(noob_allocator.has_value());
    return noob_allocator->allocate(nbytes);
}

void noob_free(void* ptr) {
    noob_non_allocating_printf("noob_free(%p)\n", ptr);
    assert(noob_allocator.has_value());
    if (!ptr)
        return;
    noob_allocator->free(ptr);
}

void* noob_realloc(void* oldptr, size_t newsize) {
    noob_non_allocating_printf("noob_realloc(%p, %lu)\n", oldptr, newsize);
    assert(noob_allocator.has_value());
    if (!oldptr)
        return noob_allocator->allocate(newsize);
    if (!newsize) {
        noob_allocator->free(oldptr);
        return NULL;
    }
    return noob_allocator->realloc(oldptr, newsize);
}

void* noob_memalign(size_t alignment, size_t size) {
    noob_non_allocating_printf("noob_memalign(%lu, %lu)\n", alignment, size);
    assert(noob_allocator.has_value());
    if (!size)
        return NULL;
    assert(std::popcount(alignment) == 1); // pow2. might fail if 0
    size = std::max(alignment, std::bit_ceil(size));
    return noob_allocator->allocate(size);
}

void* noob_calloc(size_t nbytes) {
    noob_non_allocating_printf("noob_calloc(%lu)\n", nbytes);
    assert(noob_allocator.has_value());
    return noob_allocator->zalloc(nbytes);
}
