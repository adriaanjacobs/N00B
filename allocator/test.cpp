#include <assert.h>

#include <bitset>

int main() {

    fprintf(stderr, "Start of main!!\n");

    malloc(6969);
    void* test = malloc(6969);
    volatile int* some_bytes = (int*) malloc(6969);

    free(test);

    auto check = malloc(6969);
    assert(check == test);

    auto check2 = realloc(check, 3232);
    assert(check2 == check);

    auto check3 = realloc(check, 6969);
    assert(check3 == check2);

    auto check4 = realloc(check, 1234);
    assert(check4 != check3);

    // *some_bytes = 69;
}
