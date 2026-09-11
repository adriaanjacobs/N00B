#include "util.h"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <ios>
#include <sstream>
#include <string>

#include "NOOB/config.h"
#include "NOOB/memlayout.h"

auto noob_is_nonnoob(uintptr_t ptr) -> bool {
    return extract_power(ptr) >= NON_NOOB_MIN_POWER;
}

auto noob_embed_inpointer_tag(uintptr_t ptr, uint16_t tag) -> uintptr_t {
    assert(tag <= TOPTAG_MAX);

    auto power = extract_power(ptr);
    auto offset = ptr & ((1ULL << power) - 1);

    ptr >>= power;
    ptr &= ~static_cast<uint64_t>(TOPTAG_MAX);
    ptr |= tag;
    ptr <<= power;
    ptr |= offset;

    return ptr;
}

auto noob_embed_top_tag(uintptr_t ptr, uint16_t tag) -> uintptr_t {
    assert(tag <= TOPTAG_MAX);
    return ptr | static_cast<uint64_t>(tag) << NOOB_TOPTAG_START;
}

auto noob_embed_top_tag(uintptr_t ptr) -> uintptr_t {
    return noob_embed_top_tag(ptr, extract_inpointertag(ptr));
}

auto noob_ptr_arithmetic_valid(const void* ptr, const void* base) -> bool {
    const uintptr_t ptrint = reinterpret_cast<uintptr_t>(ptr);
    const uintptr_t baseint = reinterpret_cast<uintptr_t>(base);

    const uint8_t power = extract_power(baseint);
    const uint64_t mask_invariant_bits = (~static_cast<uint64_t>(TOPTAG_MAX)) << power;
    const uint64_t arith_area_size = ~mask_invariant_bits;
    const unsigned long aritharea_base = baseint & mask_invariant_bits;
    const unsigned long offset = ptrint - aritharea_base;

    return offset < arith_area_size;
}

#if __aarch64__
#include <linux/prctl.h>
#include <sys/prctl.h>
auto enable_tbi() -> bool {
    return prctl(PR_SET_TAGGED_ADDR_CTRL, PR_TAGGED_ADDR_ENABLE, 0, 0, 0, 0) == 0;
}
#endif

#if __x86_64__
#include <asm/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

auto enable_lam() -> bool {
    uint64_t max_tag_bits;
    constexpr int8_t LAM_BITS = 15;
    constexpr uint64_t LAM_MASK = ~(~0x0UL << LAM_BITS) << (63 - LAM_BITS);

    if (syscall(SYS_arch_prctl, ARCH_GET_MAX_TAG_BITS, &max_tag_bits) != 0) {
        return false;
    }

    if (max_tag_bits != LAM_BITS) {
        // LAM support isn't enabled on this kernel.
        return false;
    }

    if (syscall(SYS_arch_prctl, ARCH_ENABLE_TAGGED_ADDR, LAM_BITS) != 0) {
        return false;
    }

    uint64_t untag_mask = 0;
    if (syscall(SYS_arch_prctl, ARCH_GET_UNTAG_MASK, &untag_mask) != 0) {
        std::abort();
    }
    if (untag_mask != ~LAM_MASK) {
        // Somehow the kernel's LAM U48 mask is different than expected?
        return false;
    }

    return true;
}
#endif

auto noob_region_initialized() -> bool {
    std::string line;
    size_t address_start = size_region_base(NOOB_MIN_POWER);
    size_t address_end = size_region_base(NOOB_MIN_POWER);
    char skip;
    bool first = true;
    std::ifstream maps("/proc/self/maps");

    if (!maps.is_open()) {
        // Failed to open our own process maps.
        std::abort();
    }

    while (std::getline(maps, line) && !noob_is_nonnoob(address_end)) {
        std::istringstream iss(line);

        if (!(iss >> std::hex >> address_start)) {
            std::abort();
        }
        if (first && !(address_start <= size_region_base(NOOB_MIN_POWER))) {
            return false;
        }
        if (!first && address_start != address_end) {
            return false;
        }
        if (!(iss >> std::hex >> skip >> address_end)) {
            std::abort();
        };
        first = false;
    }

    // If we didn't loop once, there was no reservation in the N00B region.
    return !first;
}
