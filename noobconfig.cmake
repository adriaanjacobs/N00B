set(TAG_WIDTH                   8)
set(ARITH_LEEWAY_WIDTH          0)
set(ARITH_LEEWAY_OCCUPIED_BITS  0b00)
set(NOOB_STACK_SIZE             0x400000ULL) # bzip2 needs large objects on the stack!
math(EXPR
    # minimum address of non-NOOB managed memory. 
    # maximum address of NOOB-managed memory (in practice: (42 - TAG_WIDTH - ARITH_LEEWAY_WIDTH) << 42)
    # should be small enough to leave room for non-NOOB managed memory
    #   i.e. definitely smaller than 0x800000000000 (radix: 32)
    #   with TAG_WIDTH=8, this means ARITH_LEEWAY_WIDTH should be at least 
    NOOB_COMPAT_MIN_ADDR        "(64 - ${TAG_WIDTH} - ${ARITH_LEEWAY_WIDTH}) << 42" 
OUTPUT_FORMAT HEXADECIMAL)

set(CHECK_POINTER_DEREFERENCES  1)
set(CHECK_POINTER_ARITHMETIC    1)
set(REPLACE_STACK_ALLOCS        1)
set(REMAP_GLOBALS               0)

set(EMIT_RUNTIME_CALLS          0)
