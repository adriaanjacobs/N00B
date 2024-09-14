#include <assert.h>
#include <malloc.h>

int main() {

    fprintf(stderr, "Start of main!!\n");

    malloc(16);
    void* test = malloc(16);
    volatile int* some_bytes = (int*) malloc(16);

    free(test);

    auto check = malloc(16);
    assert(check == test);

    auto check2 = realloc(check, 8);
    assert(check2 == check);

    auto check3 = realloc(check2, 16);
    assert(check3 == check2);

    auto check4 = realloc(check3, 1234);
    assert(check4 != check3);

    // *some_bytes = 69;

    auto aligned = memalign(32, 1000);

    free(check4);
    free(aligned);

    auto ptr = (int*) calloc(16, sizeof(int));
    for (int i = 0; i < 16; i++)
        assert(ptr[i] == 0);
}
