set(TAG_WIDTH                   8)
set(ARITH_LEEWAY_WIDTH          0)
set(NOOB_STACK_SIZE             0x400000ULL) # bzip2 needs large objects on the stack!

set(TAG_POINTERS                1)
set(CHECK_POINTER_DEREFERENCES  1)
set(CHECK_POINTER_ARITHMETIC    1)
set(ARITH_CHECK_BRANCH          0)
if (NOT ${CHECK_POINTER_ARITHMETIC})
    set(ARITH_CHECK_BRANCH      0)
endif()
set(REPLACE_STACK_ALLOCS        1)
set(REMAP_GLOBALS               1)

set(EMIT_RUNTIME_CALLS          0)
