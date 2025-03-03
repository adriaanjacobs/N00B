# N00B config file. Defaults are set for reproducible performance evaluation, across both x86 and ARM

# encode platform differences here
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
    set(TAG_WIDTH               7)
    # minimum 16B allocations
    set(NOOB_MIN_RADIX          0x4)
    # maximum 16GB allocations
    set(NON_NOOB_MIN_RADIX      35)
    set(NOOB_IGNORE_ERRORS      1)
    set(NOOB_TAG_POINTERS       0)
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    set(TAG_WIDTH               8)
    # no minimum allocation size
    #   the implementation may enforce additional minima for performance/simplicity
    #   but this setting guarantees that the actual radix will be encoded natively in the pointer
    set(NOOB_MIN_RADIX          0x0)
    # allocator may enforce lower limits (e.g. 34), but mapping non-noob above this
    #   ensures that it is transparently ignored by our instrumentation, without explicit masking
    #   48 is the magical value where all instrumentation is transparently ignored
    set(NON_NOOB_MIN_RADIX      48)
    set(NOOB_IGNORE_ERRORS      0)
    set(NOOB_TAG_POINTERS       1)
else()
    message(FATAL_ERROR "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()
set(ARITH_LEEWAY_WIDTH          0)
set(ARITH_LEEWAY_OCCUPIED_BITS  0b00)
set(NOOB_STACK_SIZE             0x400000ULL) # bzip2 needs large objects on the stack!

set(CHECK_DEREFERENCE_SITES     1)
set(CHECK_ESCAPE_SITES          1)
set(CHECK_POINTER_ARITHMETIC    1)
set(CHECK_POINTER_DEREFERENCES  1)

set(REPLACE_STACK_ALLOCS        1)
set(REMAP_GLOBALS               0)

set(EMIT_RUNTIME_CALLS          0)
