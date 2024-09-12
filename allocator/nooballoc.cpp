#include "nooballoc.h"

#include <errno.h>
#include <stddef.h>
#include <assert.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdint.h>

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

inline uintptr_t size_region_base(size_t radix) { 
    return radix << 42; 
}

inline size_t size_region_size() {
    return (1ULL << 42); 
}

inline size_t block_size(size_t radix) {
    return (1ULL << radix);
}

inline size_t extract_radix(uintptr_t ptr) {
    return (ptr >> 42) & 0b0011'1111;
}

inline size_t extract_lowestMSBs(uintptr_t ptr) {
    auto radix = extract_radix(ptr);
    return (ptr >> radix) & (~0ULL >> (64 - TAG_WIDTH));
}

struct NOOBArena {
    const size_t radix;
    void* base;
    void* bottom_of_three_base;
    std::bitset<NUM_BLOCKS_IN_ARENA> free_status;

    size_t single_arena_size() const {
        return NUM_BLOCKS_IN_ARENA * block_size(radix);
    }

    NOOBArena(size_t radix, void* suggested_location) :
        radix{radix}
    {
        free_status.flip(); // set all to free
        auto aloc = (uintptr_t) suggested_location;
        do {
            // check if available
            auto possible_base = mmap64((void*) aloc, 3*single_arena_size(), PROT_NONE, MAP_ANON|MAP_PRIVATE|MAP_NORESERVE|MAP_FIXED_NOREPLACE, -1, 0);
            if (possible_base != MAP_FAILED)
                break;
            ASSERT_ELSE_PERROR(errno == EEXIST);

            // increment & mask
            aloc = aloc + ((aloc - size_region_base(radix)) % size_region_size());
        } while (aloc != (uintptr_t) suggested_location); // round-robin try out all the possibilities

        // we've found one at aloc
        bottom_of_three_base = (void*) aloc;
        base = (void*) (aloc + single_arena_size());
        // get rw access to the actual arena in the middle
        ASSERT_ELSE_PERROR(mprotect(base, single_arena_size(), PROT_READ|PROT_WRITE) == 0);
    }

    void* allocate() {
        auto idx = free_status._Find_first();
        if (idx == free_status.size())
            return nullptr;

        // use this one
        free_status[idx] = false;

        // calculate the address of the block at this idx
        auto ptr = ((uintptr_t) base) + idx * block_size(radix);
        assert(extract_radix(ptr) == radix);
        
        // embed the lowestMSBs in the top bits now
        auto mask = extract_lowestMSBs(ptr) << (64 - TAG_WIDTH);
        ptr ^= mask;
        return (void*) ptr;
    }

    bool is_full() const {
        return free_status.none();
    }
};

struct NOOBSizeAllocator {
    const size_t radix;
    std::vector<NOOBArena> arenas;

    NOOBSizeAllocator(size_t radix) : 
        radix{radix}
    {}

    void* allocate() {
        void* suggested_base = (void*) size_region_base(radix);
        for (auto& arena : arenas) {
            suggested_base = (void*) ((uintptr_t) arena.bottom_of_three_base + 3*arena.single_arena_size());
            if (arena.is_full())
                continue;
            
            return arena.allocate();
        }

        auto& arena = arenas.emplace_back(radix, suggested_base);
        auto ret = arena.allocate();
        assert(ret);
        return ret;
    }
};

struct NOOBAllocator {
    // determines maximum allocation size
    const size_t max_radix;
    const size_t min_radix = 4;

    std::vector<NOOBSizeAllocator> per_size_allocators;

    NOOBAllocator(size_t max_radix) :
        max_radix{max_radix}
    {
        assert(max_radix > min_radix && max_radix < (42 - TAG_WIDTH));
        for (uint radix = min_radix; radix <= max_radix; radix++) {
            per_size_allocators.push_back(NOOBSizeAllocator{radix});
        }
    }

    NOOBSizeAllocator& getSizeAllocatorFor(size_t allocation_size) {
        if (allocation_size < 16)
            allocation_size = 16;
        size_t index = std::countr_zero(std::bit_ceil(allocation_size)) - min_radix;
        assert(index < per_size_allocators.size());
        return per_size_allocators.at(index);
    }

    void* allocate(size_t nbytes) {
        return getSizeAllocatorFor(nbytes).allocate();
    }
};


std::optional<NOOBAllocator> noob_allocator = std::nullopt;

void noob_init(size_t max_radix) {
    assert(!noob_allocator.has_value() && "NOOB is already initialized!");
    noob_allocator.emplace(max_radix);
}

void* noob_allocate(size_t nbytes) {
    assert(noob_allocator.has_value());
    return noob_allocator->allocate(nbytes);
}
