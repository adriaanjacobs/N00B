#include "nooballoc.h"

#include <assert.h>

#include <bitset>

int main() {
    noob_init(42 - TAG_WIDTH - 1);
    noob_allocate(16);
    void* test = noob_allocate(16);
    volatile int* some_bytes = (int*) noob_allocate(16);

    noob_free(test);

    auto check = noob_allocate(16);
    assert(check == test);

    // *some_bytes = 69;
}
