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

    auto check2 = noob_realloc(check, 8);
    assert(check2 == check);

    auto check3 = noob_realloc(check, 16);
    assert(check3 == check2);

    auto check4 = noob_realloc(check, 1234);
    assert(check4 != check3);

    // *some_bytes = 69;
}
