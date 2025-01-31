#include <stdio.h>
#include <stdlib.h>



int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <num>\n", argv[0]);
        exit(-1);
    }

    int* buf = (int*) malloc(10 * sizeof(int));
    for (int i = 0; i < 10; i++) {
        buf[i] = rand();
    }

    int i = atoi(argv[1]);

    fprintf(stderr, "Element at [%d] is %d\n", i, buf[i]);

    free(buf);
}