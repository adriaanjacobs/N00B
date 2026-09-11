#include "log.h"

#include <cstdint>
#include <cstdio>

#include "NOOB/memlayout.h"
#include "util.h"

thread_local int64_t traced_invocation = 0;

namespace nooballoc::log {

bool logging_enabled = false;

}

void noob_print_ptr(const char* prefix, const void* ptr) {
    fprintf(stderr, "%s: %p\n", prefix, ptr);
    const uintptr_t ptrint = reinterpret_cast<uintptr_t>(ptr);
    if (noob_is_nonnoob(ptrint)) {
        fprintf(stderr, "\tnon-noob managed pointer\n");
        return;
    }

    fprintf(stderr, "\tpower:  %u\n", extract_power(ptrint));
    fprintf(stderr, "\ttoptag: 0x%x\n", extract_toptag(ptrint));
    fprintf(stderr, "\tintag:  0x%x\n", extract_inpointertag(ptrint));
    fprintf(stderr, "\toffset: 0x%lx\n", ptrint & ((1U << extract_power(ptrint)) - 1));
}
