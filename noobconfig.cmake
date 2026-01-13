# N00B config file. Defaults are set for reproducible performance evaluation, across both x86 and ARM

# encode platform differences here
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)")
    message(STATUS "Host: AArch64 (ARM)")
    set(TAG_WIDTH               8)
    set(NOOB_TOPTAG_START       56) # amount to shift down by to obtain the toptag. N00B assumes that all bits above the toptag are 0
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
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)")
    # figure out if this is Intel or AMD
    file(READ "/proc/cpuinfo" CPUINFO_RAW)
    if(CPUINFO_RAW MATCHES "vendor_id\t: GenuineIntel")
        message(STATUS "Host: Intel x86_64 (Detected GenuineIntel)")
        set(TAG_WIDTH               8)
        set(NOOB_TOPTAG_START       55)
        # minimum 16B allocations
        set(NOOB_MIN_RADIX          0x4)
        # maximum 16GB allocations
        set(NON_NOOB_MIN_RADIX      35)
        set(NOOB_IGNORE_ERRORS      0)
        set(NOOB_TAG_POINTERS       1)
    elseif(CPUINFO_RAW MATCHES "vendor_id\t: AuthenticAMD")
        message(STATUS "Host: AMD x86_64 (Detected AuthenticAMD)")
        set(TAG_WIDTH               7)
        set(NOOB_TOPTAG_START       56)
        # minimum 16B allocations
        set(NOOB_MIN_RADIX          0x4)
        # maximum 16GB allocations
        set(NON_NOOB_MIN_RADIX      35)
        set(NOOB_IGNORE_ERRORS      1)
        set(NOOB_TAG_POINTERS       0)
    else()
        message(FATAL_ERROR "Host: x86_64, but Vendor ID is unknown: ${HOST_PROC}")
    endif()
else()
    message(FATAL_ERROR "Unsupported processor: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

math(EXPR NOOB_MAX_ADDR "(${NON_NOOB_MIN_RADIX}-${NOOB_MIN_RADIX})<<42" OUTPUT_FORMAT HEXADECIMAL)

set(ARITH_LEEWAY_WIDTH          0)
set(ARITH_LEEWAY_OCCUPIED_BITS  0b00)
set(NOOB_STACK_SIZE             0x400000ULL) # bzip2 needs large objects on the stack!

set(CHECK_DEREFERENCE_SITES     1)
set(CHECK_ESCAPE_SITES          1)
set(CHECK_POINTER_ARITHMETIC    1)
set(CHECK_POINTER_DEREFERENCES  1)
set(USE_BRANCHING_CHECKS        1)
set(SOUND_POINTER_DETECTION     1)

set(MASK_EXT_PTR_ARGS           0)
set(COUNT_NOOB_FAILURES         0)

set(REPLACE_STACK_ALLOCS        1)
set(REMAP_GLOBALS               0)

set(EMIT_RUNTIME_CALLS          0)
set(TRACK_ARITH_STATS           0)

set(EMULATE_LOWFAT              0)
set(PAD_BY_ONE_BYTE             0)
