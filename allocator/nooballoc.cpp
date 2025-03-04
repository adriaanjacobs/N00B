#include "nooballoc.h"

#include <NOOB/config.h>
#include <NOOB/memlayout.h>

#include <errno.h>
#include <linux/prctl.h>
#include <stddef.h>
#include <assert.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/prctl.h>

#include <vector>
#include <bit>
#include <bitset>
#include <map>
#include <functional>

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

struct run_on_destruct {
    std::function<void()> func;
    run_on_destruct(auto func) : func{std::move(func)} {}
    ~run_on_destruct() { func(); }
};

#define CONCAT_(x,y) x##y
#define CONCAT(x,y) CONCAT_(x,y)
#define UNIQUE_VAR_NAME CONCAT(_unique_var_, __COUNTER__)
#define defer(block) run_on_destruct UNIQUE_VAR_NAME{[&] () -> void { block; }}

#define NUM_BLOCKS_IN_ARENA         (1U << TAG_WIDTH)
#define TAG_T_MAX                   (NUM_BLOCKS_IN_ARENA - 1UL)

bool noob_is_nonnoob(uintptr_t ptr) {
    return extract_radix(ptr) >= NON_NOOB_MIN_RADIX;
}

void noob_print_ptr(const char* prefix, void* ptr) {
    fprintf(stderr, "%s: %p\n", prefix, ptr);
    auto ptrint = (uintptr_t) ptr;
    if (noob_is_nonnoob(ptrint)) {
        fprintf(stderr, "\tnon-noob managed pointer\n");
        return;
    }
    
    fprintf(stderr, "\tradix:  %u\n", extract_radix(ptrint));
    fprintf(stderr, "\ttoptag: 0x%x\n", extract_toptag(ptrint));
    fprintf(stderr, "\tintag:  0x%x\n", extract_inpointertag(ptrint));
    fprintf(stderr, "\toffset: 0x%lx\n", ptrint & ((1U << extract_radix(ptrint)) - 1));
}

size_t arena_idx_in_size_region(uintptr_t ptr, uint8_t radix) {
    return (ptr - size_region_base(radix)) / single_arena_size(radix);
}

size_t arena_idx_in_size_region(uintptr_t ptr) {
    auto radix = extract_radix(ptr);
    return arena_idx_in_size_region(ptr, radix);
}

inline size_t arena_base(uintptr_t ptr) {
    auto radix = extract_radix(ptr);
    ptr &= (~0ULL >> TAG_WIDTH); // clear the top bits
    ptr &= ~static_cast<uint64_t>(TAG_T_MAX) << radix; // clear the iptag & offset
    return ptr;
}

// finds & returns an size-aligned mapping of size `size` containing `in_pointer_radix` in the radix bits
static void* mmap_arena_aritharea(uint8_t in_pointer_radix, size_t aritharea_size, void* suggested_location) {
    assert(extract_radix((uintptr_t) suggested_location) == in_pointer_radix);
    assert(aritharea_size >= 0x1000);
    assert(aritharea_size % 0x1000 == 0);
    assert(aritharea_size % 2 == 0);
    assert(__builtin_is_aligned(suggested_location, aritharea_size));
    assert(size_region_size() % aritharea_size == 0);
    if (aritharea_size > single_arena_size(in_pointer_radix))
        assert(aritharea_size % single_arena_size(in_pointer_radix) == 0);
    else
        assert(single_arena_size(in_pointer_radix) % aritharea_size == 0);
    auto align_to = std::max(aritharea_size, single_arena_size(in_pointer_radix));

    // round-robin try out all the possibilities
    auto aloc = (uintptr_t) suggested_location;
    while (true) {
        // check if available
        auto possible_base = mmap64((void*) aloc, aritharea_size, PROT_NONE, MAP_ANON|MAP_PRIVATE|MAP_NORESERVE|MAP_FIXED_NOREPLACE, -1, 0);
        if (possible_base != MAP_FAILED)
            break;
        ASSERT_ELSE_PERROR(errno == EEXIST);

        // increment & mask
        aloc += align_to;
        aloc = size_region_base(in_pointer_radix) + ((aloc - size_region_base(in_pointer_radix)) % size_region_size());
        
        // if we've gone all the way around, then we couldn't find any possible mapping
        assert(aloc != (uintptr_t) suggested_location && "No more VM space available");
    }
    // we've found one at aloc
    assert((void*) aloc != MAP_FAILED);
    assert(aloc % aritharea_size == 0);
    assert(aloc % single_arena_size(in_pointer_radix) == 0);
    return (void*) aloc;
}

struct NOOBArena {
    std::bitset<NUM_BLOCKS_IN_ARENA> free_status;
    // fresh blocks have never been allocated before
    //  they are, by definition, free
    std::bitset<NUM_BLOCKS_IN_ARENA> fresh_status;
    void* aritharea_base;
    void* occupied_base;
    const uint8_t radix;

    bool is_full() const {
        return free_status.none();
    }

    bool has_fresh_blocks() const {
        return fresh_status.any();
    }

    bool contains(void* ptr) {
        return arena_base((uintptr_t) ptr) == (uintptr_t) occupied_base;
    }

    size_t idx_in_containing_size_region() const {
        return arena_idx_in_size_region((uintptr_t) occupied_base, radix);
    }

    NOOBArena(uint8_t radix, void* location, bool is_suggestion = true) :
        radix{radix}
    {
        free_status.flip(); // set all to free
        fresh_status.flip(); // at first, all blocks are fresh

        {   // set (at least) the first and last cache line of every arena to "allocated"
            //  this is a poor man's way to ensure benign arithmetic-capable boundary objects
            auto num_indices = std::max(1UL, 64UL / (1UL << radix));
            for (uint i = 0; i < num_indices; i++) {
                auto complementary_i = free_status.size() - 1 - i;
                free_status[i] = false;
                free_status[complementary_i] = false;
                fresh_status[i] = false;
                fresh_status[complementary_i] = false;
            }
            assert(free_status.any()); // otherwise this arena is so small we just filled it with arithmetic padding
        }

        aritharea_base = !is_suggestion ? location : mmap_arena_aritharea(radix, arith_area_size(radix), location);
        // get rw access to the actual arena in the "middle"
        occupied_base = (void*) (((uintptr_t) aritharea_base) + ARITH_LEEWAY_OCCUPIED_BITS * single_arena_size(radix));
        if (is_suggestion)
            ASSERT_ELSE_PERROR(mprotect(occupied_base, single_arena_size(radix), PROT_READ|PROT_WRITE) == 0);

        assert((uintptr_t) aritharea_base % single_arena_size(radix) == 0);
        assert((uintptr_t) occupied_base % single_arena_size(radix) == 0);
    }

    void* allocate(uint idx) {
        // use this one
        free_status[idx] = false;
        fresh_status[idx] = false;

        // calculate the address of the block at this idx
        auto ptr = ((uintptr_t) occupied_base) + idx * block_size(radix);
        assert(extract_radix(ptr) == radix);

#if NOOB_TAG_POINTERS
        // embed the lowestMSBs in the top bits now
        auto iptag = extract_inpointertag(ptr);
        assert(iptag <= TAG_T_MAX);
        auto topmask = static_cast<uint64_t>(iptag) << (64 - TAG_WIDTH);
        ptr |= topmask;
#endif
        return (void*) ptr;
    }

    void* allocate() {
        auto idx = free_status._Find_first();
        assert(idx < free_status.size());
        return allocate(idx);
    }

    void free(void* ptr) {
        auto lowestMSBs = extract_inpointertag((uintptr_t) ptr);
        uint idx = lowestMSBs - extract_inpointertag((uintptr_t) occupied_base);
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
    // quick lookup during `free`: map arena_idx to arena
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
            suggested_base = (void*) ((uintptr_t) arena.occupied_base + single_arena_size(radix));
        // for non-0x1000-aligned single_arena bases, align up
        suggested_base = __builtin_align_up(suggested_base, 0x1000);

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
        //  sometimes, we have to create more than one arena here to fill the minimum amount of memory we get from the OS
        auto new_arenas_to_create = 1;
        auto multiarena_base = (uint8_t*) figure_out_a_good_base_suggestion();
        bool multiarena = single_arena_size(radix) < 0x1000;
        if (multiarena) {
            assert(0x1000 % single_arena_size(radix) == 0);
            new_arenas_to_create = 0x1000/single_arena_size(radix);
            multiarena_base = (uint8_t*) mmap_arena_aritharea(radix, 0x1000, figure_out_a_good_base_suggestion());
            ASSERT_ELSE_PERROR(mprotect(multiarena_base, 0x1000, PROT_READ|PROT_WRITE) == 0);
        }

        // iterate through the arena space. for non-multiarenas, this loop executes just once
        do {
            new_arenas_to_create--;
            void* base = multiarena_base + new_arenas_to_create * single_arena_size(radix);

            NOOBArena arena{radix, base, !multiarena};
            // i have to explicitly compute this upfront since there is no standard function argument evaluation order
            auto arena_idx = arena.idx_in_containing_size_region(); 
            auto [it, inserted] = arenas.try_emplace(arena_idx, std::move(arena));
            assert(inserted);
            arenas_with_free_entries.push_back(it);

        } while (new_arenas_to_create);
        
        return {arenas_with_free_entries.back(), true};
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
        auto [_arena_it, is_definitely_fresh] = get_or_create_arena(true);
        auto arena_it = _arena_it; // work around dump local binding clang bug
        defer ( // i'm definitely going to allocate, check _afterwards_ if i should remove the arena
            if (arena_it->second.is_full()) {
                // it's not necessarily the last one here!
                auto it = std::find(arenas_with_free_entries.begin(), arenas_with_free_entries.end(), arena_it);
                assert(it != arenas_with_free_entries.end());
                arenas_with_free_entries.erase(it);
            }
        );

        if (is_definitely_fresh) // we either found fresh blocks in this arena or we just created it
            return arena_it->second.zalloc();
        
        // we found some non-fresh blocks that we still have to zero
        auto block = arena_it->second.allocate();
        memset(noob_striptop(block), 0, block_size(radix));
        return block;
    }
};

// if we are linking into a hardened program, this function will be defined by the NOOB compiler
extern "C" void noob_initialize_noobstacks() __attribute__((weak));

struct NOOBAllocator {
    std::vector<NOOBSizeAllocator> per_size_allocators;
    // determines maximum allocation size
    const uint8_t max_radix;
    const uint8_t min_radix = std::max(4, NOOB_MIN_RADIX);

    __attribute__((optnone))
    NOOBAllocator(uint8_t max_radix) :
        max_radix{max_radix}
    {
        fprintf(stderr, "Initializing NOOB...\n");
        
        if (noob_initialize_noobstacks) // the function exists. we are linked into a hardened NOOB program
            noob_initialize_noobstacks();

#if __aarch64__
        // start by enabling the tagged address ABI
        if (prctl(PR_SET_TAGGED_ADDR_CTRL, PR_TAGGED_ADDR_ENABLE, 0, 0, 0, 0) == -1)
            perror("enable tagged address kernel abi");
#endif // on x86-64, there is no kernel interface yet

        // The non-NOOB memory region is mapped at address 2^NON_NOOB_MIN_RADIX -> VA_MAX. Instrumentation ignores these pointers as follows:
        //  the dereferences checks:
        //      on x86 UAI: will explicitly ignore pointers with that radix
        //      on AArch64: will implicitly ignore those pointers because the NON_NOOB_MIN_RADIX is 48, which means that the "in-pointer tag" will be read as 0 from the upper bits, and the toptag too.
        //  the arithmetic check will "transparently" do so, based on the assumption that non-noob memory object are supposedly "huge" (i.e. 2^NON_NOOB_MIN_RADIX bytes), and allocated in arenas that are 2^TAG_WIDTH * 2^NON_NOOB_MIN_RADIX bytes large
        //      the idea is that arithmetic on those is unlikely to escape the arena boundaries. Arena boundaries exist at multiples of 2^TAG_WIDTH * 2^NON_NOOB_MIN_RADIX
        //          on AArch64 systems, this is 2^8 * 2^48 = 2^56 bytes, which is larger than the entire virtual address space (2^48) -> no real arithmetic restrictions. 
        //          on UAI x86 systems, this is 2^7 * 2^35 = 2^42 bytes, which is the size of a size region -> we will effectively restrict arithmetic to within the size region boundaries. 
        //              to ensure that this does not cause issues for non-noob-managed memory that is allocated near these boundaries, we map a page at the start and end of the non-noob-managed memory region
        {
            auto compat_guard_start = mmap((void*)size_region_base(NON_NOOB_MIN_RADIX), 0x1000, PROT_NONE, MAP_FIXED_NOREPLACE|MAP_ANON|MAP_PRIVATE|MAP_NORESERVE, -1, 0);
            if (compat_guard_start == MAP_FAILED)
                ASSERT_ELSE_PERROR(errno == EEXIST);
            auto compat_guard_end = mmap((void*)(size_region_base(NON_NOOB_MIN_RADIX) + size_region_size() - 0x2000), 0x1000, PROT_NONE, MAP_FIXED_NOREPLACE|MAP_ANON|MAP_PRIVATE|MAP_NORESERVE, -1, 0);
            if (compat_guard_start == MAP_FAILED)
                ASSERT_ELSE_PERROR(errno == EEXIST);
        }

        assert(max_radix > min_radix && max_radix < NON_NOOB_MIN_RADIX && max_radix <= (42 - TAG_WIDTH));
        for (uint8_t radix = min_radix; radix <= max_radix; radix++) {
            per_size_allocators.push_back(NOOBSizeAllocator{radix});
        }

        hooked = true;
    }

    ~NOOBAllocator() {
        // ensure that our deallocation will not trigger a bunch of recursive `free` invocations
        hooked = false;
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
#if NOOB_TAG_POINTERS
        // now for a quick security check
        // check that it is still pointing to the original alloc
        assert(extract_inpointertag((uintptr_t) ptr) == extract_toptag((uintptr_t) ptr));
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

// never dealloc
NOOBAllocator* noob_allocator = new NOOBAllocator(std::min(34, NON_NOOB_MIN_RADIX - 1));

void* noob_malloc(size_t nbytes) {
    assert(!hooked);
    return noob_allocator->allocate(nbytes);
}

void noob_free(void* ptr) {
    assert(!hooked);
    if (!ptr)
        return;
    noob_allocator->free(ptr);
}

void* noob_realloc(void* oldptr, size_t newsize) {
    assert(!hooked);
    if (!oldptr)
        return noob_allocator->allocate(newsize);
    if (!newsize) {
        noob_allocator->free(oldptr);
        return NULL;
    }
    return noob_allocator->realloc(oldptr, newsize);
}

void* noob_memalign(size_t alignment, size_t size) {
    assert(!hooked);
    if (!size)
        return NULL;
    assert(std::popcount(alignment) == 1); // pow2. might fail if 0
    size = std::max(alignment, std::bit_ceil(size));
    return noob_allocator->allocate(size);
}

void* noob_calloc(size_t nbytes) {
    assert(!hooked);
    return noob_allocator->zalloc(nbytes);
}

size_t noob_usable_size(void* ptr) {
    assert(!hooked);
    auto radix = extract_radix((uintptr_t) ptr);
    return 1ULL << radix;
}

static uintptr_t noob_embed_inpointer_tag(uintptr_t ptr, uint8_t top_tag) {
    auto radix = extract_radix(ptr);
    auto offset = ptr & ((1ULL << radix) - 1);

    ptr >>= radix;
    ptr &= ~static_cast<uint64_t>(TAG_T_MAX);
    assert(top_tag <= NUM_BLOCKS_IN_ARENA - 1);
    ptr |= top_tag;
    ptr <<= radix;
    ptr |= offset;

    return ptr;
}

static void check_ptr_arithmetic(void* ptr, void* base) {
    auto baseint = (uintptr_t) base;
    auto ptrint = (uintptr_t) ptr;

    auto radix = extract_radix(baseint);
    auto mask_invariant_bits = (~static_cast<uint64_t>(TAG_T_MAX)) << (ARITH_LEEWAY_WIDTH + radix);
    auto arith_area_size = ~mask_invariant_bits;
    auto aritharea_base = baseint & mask_invariant_bits;
    auto offset = ptrint - aritharea_base;
    if (offset >= arith_area_size) {
        fprintf(stderr, "\n\nOut of bounds arithmetic detected!!\n");
        noob_print_ptr("ptr", ptr);
        noob_print_ptr("base", base);
        fprintf(stderr, "aritharea of base is [%p, %p[ (size: %lu)\n", (void*) aritharea_base, (void*) (aritharea_base + arith_area_size), arith_area_size);
        fprintf(stderr, "offset of ptr in aritharea is %ld\n", static_cast<intptr_t>(offset));
    }
    assert(offset < arith_area_size);
}

extern "C" {

void noob_allocate_stacks(void** stack_array, uint8_t lowest_radix, uint8_t highest_radix) {
    fprintf(stderr, "Allocating NOOB stacks between %d and %d...\n", lowest_radix, highest_radix);
    assert(highest_radix >= lowest_radix);
    assert(lowest_radix >= std::max(3, NOOB_MIN_RADIX) && "We don't map noobstacks for stack objects too small");
    assert((1U << highest_radix) < NOOB_STACK_SIZE && "We don't map noobstacks for objects > NOOB_STACK_SIZE");
    assert(highest_radix < NON_NOOB_MIN_RADIX);
    for (uint radix = lowest_radix; radix <= highest_radix; radix++) {
        // let's try to map this immediately at the start of the size region
        auto stack = mmap_arena_aritharea(radix, NOOB_STACK_SIZE, (void*) size_region_base(radix));
        assert(extract_radix((uintptr_t) stack) == radix);
        assert(extract_toptag((uintptr_t) stack) == 0); // stackptr shouldnt be toptagged
        // we leave a guard page at the end here to detect stack overflow
        ASSERT_ELSE_PERROR(mprotect(stack, NOOB_STACK_SIZE - 0x1000, PROT_READ|PROT_WRITE) == 0);
        stack_array[radix] = stack;
    }
}

void noob_access_check(void* ptr, void* base, bool checkDeref, bool checkArith) {
    // fprintf(stderr, "noob_access_check(%p, %p)\n", ptr, base);
    if (checkArith)
        check_ptr_arithmetic(ptr, base);

    if (!checkDeref)
        return;

    auto radix = extract_radix((uintptr_t) base);
    auto embedded_tag = extract_inpointertag((uintptr_t) ptr);
    auto top_tag = extract_toptag((uintptr_t) ptr);

    if (radix >= NON_NOOB_MIN_RADIX) // ignore uninstrumented pointers
        return;
    
    if (embedded_tag != top_tag) {
        fprintf(stderr, "\n\nDereference check failed!!\n");
        noob_print_ptr("ptr", ptr);
        if (base != ptr)
            noob_print_ptr("base", base);

        if (noob_is_nonnoob((uintptr_t) ptr) && noob_is_nonnoob((uintptr_t) base)) 
            asm("int3");

        auto likely_base = noob_embed_inpointer_tag((uintptr_t) ptr, top_tag);
        fprintf(stderr, "likely OOB offset %ld into object at %p (size %lu)\n", ((uintptr_t)ptr) - likely_base, noob_striptop((void*) likely_base), 1UL << radix);

        fprintf(stderr, "embedded_tag: %u\n", embedded_tag);
        fprintf(stderr, "top_tag: %u\n", top_tag);
    }
    assert(embedded_tag == top_tag);

    // check if they're accessible
    auto accessptr = (volatile char*) ptr;
    *accessptr;
}

}
