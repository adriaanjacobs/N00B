#include "nooballoc.h"

#include <NOOB/config.h>

#include <errno.h>
#include <linux/prctl.h>
#include <stddef.h>
#include <assert.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/prctl.h>

#include <vector>
#include <bit>
#include <optional>
#include <bitset>
#include <map>

#define TAG_POINTERS 0

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

uintptr_t size_region_base(uint8_t radix) { 
    return ((size_t) radix) << 42; 
}

size_t size_region_size() {
    return (1ULL << 42); 
}

size_t block_size(uint8_t radix) {
    return (1ULL << radix);
}

size_t single_arena_size(uint8_t radix) {
    return NUM_BLOCKS_IN_ARENA * block_size(radix);
}

uint8_t extract_radix(uintptr_t ptr) {
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

size_t arena_idx_in_size_region(uintptr_t ptr, uint8_t radix) {
    return (ptr - size_region_base(radix)) / single_arena_size(radix);
}

size_t arena_idx_in_size_region(uintptr_t ptr) {
    auto radix = extract_radix(ptr);
    return arena_idx_in_size_region(ptr, radix);
}

struct NOOBArena {
    std::bitset<NUM_BLOCKS_IN_ARENA> free_status;
    // fresh blocks have never been allocated before
    //  they are, by definition, free
    std::bitset<NUM_BLOCKS_IN_ARENA> fresh_status;
    void* base;
    void* bottom_of_three_base;
    const uint8_t radix;

    bool is_full() const {
        return free_status.none();
    }

    bool has_fresh_blocks() const {
        return fresh_status.any();
    }

    bool contains(void* ptr) {
        return extract_highestMSBs((uintptr_t) ptr) == extract_highestMSBs((uintptr_t) base);
    }

    size_t idx_in_containing_size_region() const {
        return arena_idx_in_size_region((uintptr_t) base, radix);
    }

    NOOBArena(uint8_t radix, void* suggested_location) :
        radix{radix}
    {
        free_status.flip(); // set all to free
        fresh_status.flip(); // at first, all blocks are fresh
        auto aloc = (uintptr_t) suggested_location;
        while (true) { // round-robin try out all the possibilities
            // check if available
            auto possible_base = mmap64((void*) aloc, 3*single_arena_size(radix), PROT_NONE, MAP_ANON|MAP_PRIVATE|MAP_NORESERVE|MAP_FIXED_NOREPLACE, -1, 0);
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
        base = (void*) (aloc + single_arena_size(radix));
        // get rw access to the actual arena in the middle
        ASSERT_ELSE_PERROR(mprotect(base, single_arena_size(radix), PROT_READ|PROT_WRITE) == 0);

        // we require this alignment so we can quickly figure out which arena a given pointer belongs to
        assert((uintptr_t) base % single_arena_size(radix) == 0);
    }

    void* allocate(uint idx) {
        // use this one
        free_status[idx] = false;
        fresh_status[idx] = false;

        // calculate the address of the block at this idx
        auto ptr = ((uintptr_t) base) + idx * block_size(radix);
        assert(extract_radix(ptr) == radix);
        
#if TAG_POINTERS
        // embed the lowestMSBs in the top bits now
        auto mask = extract_lowestMSBs(ptr) << (64 - TAG_WIDTH);
        ptr ^= mask;
#endif
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
    // quick lookup during `free`
    std::map<uintptr_t, NOOBArena> arenas;
    // if there are any free arenas, they will be in the back here
    //  requires `arenas` iterators to be stable
    std::vector<decltype(arenas)::iterator> arenas_with_free_entries;
    const uint8_t radix;

    NOOBSizeAllocator(uint8_t radix) : 
        radix{radix}
    {}

    void* figure_out_a_good_base_suggestion() {
        void* suggested_base = (void*) size_region_base(radix);
        for (auto& [_, arena] : arenas) 
            suggested_base = (void*) ((uintptr_t) arena.bottom_of_three_base + 3*single_arena_size(radix));

        if (extract_radix((uintptr_t) suggested_base) != radix)
            suggested_base = (void*) size_region_base(radix);
        return suggested_base;
    }

    // returns {it: arena_it, bool: arena_definitely_has_fresh_blocks}
    std::pair<decltype(arenas)::iterator, bool> get_or_create_arena(bool prefer_fresh) {
        if (prefer_fresh) {
            // first check whether we can find any fresh blocks that we dont have to zero
            for (auto it = arenas_with_free_entries.rbegin(); it != arenas_with_free_entries.rend(); it++) {
                auto arena_it = *it;
                if (arena_it->second.has_fresh_blocks())
                    return {arena_it, true};
            }
        }

        // if we cannot find fresh blocks, or we dont care about freshness, just find a free block
        // if there are any free arenas, they'll be in the back of `arenas_with_free_entries`
        if (!arenas_with_free_entries.empty() && !arenas_with_free_entries.back()->second.is_full())
            return {arenas_with_free_entries.back(), false};

        // there are no free entries in any of the arenas, create a new one
        NOOBArena arena{radix, figure_out_a_good_base_suggestion()};
        // i have to explicitly compute this upfront since there is no standard function argument evaluation order
        auto arena_idx = arena.idx_in_containing_size_region(); 
        auto [it, inserted] = arenas.try_emplace(arena_idx, std::move(arena));
        assert(inserted);
        arenas_with_free_entries.push_back(it);
        return {it, true};
    }

    void* allocate() {
        auto arena_it = get_or_create_arena(false).first;
        auto ret = arena_it->second.allocate();
        assert(ret);
        // if we just filled this arena, make sure it is removed from the free arenas
        if (arena_it->second.is_full()) {
            assert(arena_it == arenas_with_free_entries.back());
            arenas_with_free_entries.pop_back();
        }
        return ret;
    }

    void free(void* ptr) {
        auto arena_idx = arena_idx_in_size_region((uintptr_t) noob_striptop(ptr));
        auto arena_it = arenas.find(arena_idx);
        assert(arena_it != arenas.end() && "Cannot find block to free in any arena??");
        bool was_full = arena_it->second.is_full();
        arena_it->second.free(ptr);
        // if the arena was full, reintroduce it to the list of free arenas
        if (was_full) 
            arenas_with_free_entries.push_back(arena_it);
    }

    void* zalloc() {
        auto [arena_it, is_definitely_fresh] = get_or_create_arena(true);
        if (is_definitely_fresh) // we either found fresh blocks in this arena or we just created it
            return arena_it->second.zalloc();
        
        // we found some non-fresh blocks that we still have to zero
        auto block = arena_it->second.allocate();
        memset(noob_striptop(block), 0, block_size(radix));
        return block;
    }
};

struct NOOBAllocator {
    std::vector<NOOBSizeAllocator> per_size_allocators;
    bool* const hooked;
    // determines maximum allocation size
    const uint8_t max_radix;
    const uint8_t min_radix = 4;

    NOOBAllocator(uint8_t max_radix, bool* hooked) :
        hooked{hooked},
        max_radix{max_radix}
    {
#if TAG_POINTERS
        // start by enabling the tagged address ABI
        if (prctl(PR_SET_TAGGED_ADDR_CTRL, PR_TAGGED_ADDR_ENABLE, 0, 0, 0, 0) == -1)
            perror("enable tagged address kernel abi");
#endif
        assert(max_radix > min_radix && max_radix < (42 - TAG_WIDTH));
        for (uint8_t radix = min_radix; radix <= max_radix; radix++) {
            per_size_allocators.push_back(NOOBSizeAllocator{radix});
        }
    }

    ~NOOBAllocator() {
        // ensure that our deallocation will not trigger a bunch of recursive `free` invocations
        *hooked = false;
    }

    NOOBSizeAllocator& getSizeAllocatorForRadix(uint8_t radix) {
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
#if TAG_POINTERS
        // now for a quick security check
        // check that it is still pointing to the original alloc
        assert(extract_lowestMSBs((uintptr_t) ptr) == extract_topbits((uintptr_t) ptr));
#endif
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

std::optional<NOOBAllocator> noob_allocator = std::nullopt;

void noob_init(size_t max_radix, bool* hooked) {
    assert(!noob_allocator.has_value() && "NOOB is already initialized!");
    noob_allocator.emplace(max_radix, hooked);
}

void* noob_malloc(size_t nbytes) {
    assert(noob_allocator.has_value());
    return noob_allocator->allocate(nbytes);
}

void noob_free(void* ptr) {
    assert(noob_allocator.has_value());
    if (!ptr)
        return;
    noob_allocator->free(ptr);
}

void* noob_realloc(void* oldptr, size_t newsize) {
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
    assert(noob_allocator.has_value());
    if (!size)
        return NULL;
    assert(std::popcount(alignment) == 1); // pow2. might fail if 0
    size = std::max(alignment, std::bit_ceil(size));
    return noob_allocator->allocate(size);
}

void* noob_calloc(size_t nbytes) {
    assert(noob_allocator.has_value());
    return noob_allocator->zalloc(nbytes);
}

size_t noob_usable_size(void* ptr) {
    auto radix = extract_radix((uintptr_t) ptr);
    return 1ULL << radix;
}
