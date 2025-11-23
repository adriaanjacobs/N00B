#include <asm/prctl.h>       // ARCH_* constants, including ARCH_ENABLE_TAGGED_ADDR
#include <sys/syscall.h>
#include <unistd.h>
#include <stdint.h>          // For uint64_t
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

#include <iterator>

#define LAM_NONE                0

#define LAM_U57_BITS            6
#define LAM_U57_MASK            (0x3fULL << 57)

#define LAM_U48_BITS            15
#define LAM_U48_MASK            (0x7fffULL << 48)


int main(int argc, char* argv[]) {

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <string>\n", argv[0]);
        return -1;
    }

    unsigned long lam = LAM_U48_BITS;

    uint64_t max_tag_bits;
    if (syscall(SYS_arch_prctl, ARCH_GET_MAX_TAG_BITS, &max_tag_bits)) {
        perror("ARCH_GET_MAX_TAG_BITS");
        exit(-1);
    }

    fprintf(stderr, "Max tag bits: %lu\n", max_tag_bits);

    if (syscall(SYS_arch_prctl, ARCH_ENABLE_TAGGED_ADDR, lam)) {
        perror("ARCH_ENABLE_TAGGED_ADDR");
        exit(-1);
    }

    /* Get untagged mask */
    uint64_t untag_mask = 0;
    if (syscall(SYS_arch_prctl, ARCH_GET_UNTAG_MASK, &untag_mask)) {
        perror("ARCH_GET_UNTAG_MASK");
        exit(-1);
    }

    fprintf(stderr, "untag mask: 0x%lx, LAM U48 MASK: 0x%llx\n", untag_mask, ~LAM_U48_MASK);

    assert(untag_mask == ~LAM_U48_MASK);

    printf("Hello, %s!\n", (char*) ((uintptr_t) argv[1] | LAM_U48_MASK));

    const char message[] = "Kernel accepts tagged pointers in syscall interface!\n";
    if (syscall(SYS_write, STDERR_FILENO, ((uintptr_t) &message[0] | LAM_U48_MASK), std::size(message)) < 0) {
        perror("kernel does not accept tagged pointers in syscall interface");
        exit(-1);
    }
}
