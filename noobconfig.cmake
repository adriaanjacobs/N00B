# N00B config file. Defaults are set for reproducible performance evaluation, across both x86 and ARM

# What the tag layout below actually depends on is the hardware feature that lets us hide a tag in
#   the upper pointer bits, not the CPU as such:
#       TBI -- AArch64  Top Byte Ignore
#       LAM -- Intel    Linear Address Masking
#       UAI -- AMD      Upper Address Ignore
#   Left empty we pick the one this machine supports, which is what you want for a native build.
#   Anything else -- hardening an arm64 kernel from an x86_64 workstation, say -- has to say so:
#       cmake ... -DNOOB_TAG_SCHEME=TBI
#   Getting this wrong is silent at build time, so the pass re-checks it against the module triple.
set(NOOB_TAG_SCHEME "" CACHE STRING
    "Pointer masking extension to target: TBI, LAM or UAI. Empty detects from the build host.")
set_property(CACHE NOOB_TAG_SCHEME PROPERTY STRINGS "" TBI LAM UAI)
if(NOOB_TAG_SCHEME STREQUAL "")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
        set(NOOB_TAG_SCHEME "TBI")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$")
        file(READ "/proc/cpuinfo" CPUINFO_RAW)
        if(CPUINFO_RAW MATCHES "vendor_id\t: GenuineIntel")
            set(NOOB_TAG_SCHEME "LAM")
        elseif(CPUINFO_RAW MATCHES "vendor_id\t: AuthenticAMD")
            set(NOOB_TAG_SCHEME "UAI")
        else()
            message(FATAL_ERROR "Host is x86_64 but vendor_id is neither Intel nor AMD?")
        endif()
    else()
        message(FATAL_ERROR "No tagging scheme known for host '${CMAKE_SYSTEM_PROCESSOR}'")
    endif()
endif()

# encode platform differences here
if(NOOB_TAG_SCHEME STREQUAL "TBI")
    message(STATUS "N00B target: AArch64 TBI")
    set(TAG_WIDTH               8)
    set(NOOB_TOPTAG_START       56) # amount to shift down by to obtain the toptag. N00B assumes that all bits above the toptag are 0
    # no minimum allocation size
    #   the implementation may enforce additional minima for performance/simplicity
    #   but this setting guarantees that the actual power will be encoded natively in the pointer
    set(NOOB_MIN_POWER          0x0)
    # allocator may enforce lower limits (e.g. 34), but mapping non-noob above this
    #   ensures that it is transparently ignored by our instrumentation, without explicit masking
    #   48 is the magical value where all instrumentation is transparently ignored
    set(NON_NOOB_MIN_POWER      48)
elseif(NOOB_TAG_SCHEME STREQUAL "LAM")
    message(STATUS "N00B target: Intel LAM (x86_64)")
    set(TAG_WIDTH               8)
    set(NOOB_TOPTAG_START       55)
    # minimum 16B allocations
    set(NOOB_MIN_POWER          0x4)
    # maximum 16GB allocations
    set(NON_NOOB_MIN_POWER      35)
elseif(NOOB_TAG_SCHEME STREQUAL "UAI")
    message(STATUS "N00B target: AMD UAI (x86_64)")
    set(TAG_WIDTH               7)
    set(NOOB_TOPTAG_START       56)
    # minimum 16B allocations
    set(NOOB_MIN_POWER          0x4)
    # maximum 16GB allocations
    set(NON_NOOB_MIN_POWER      35)
else()
    message(FATAL_ERROR "Unknown NOOB_TAG_SCHEME '${NOOB_TAG_SCHEME}', expected TBI, LAM or UAI")
endif()

set(NOOB_IGNORE_ERRORS      ON)
set(NOOB_TAG_POINTERS       OFF)

math(EXPR NOOB_MAX_ADDR "(${NON_NOOB_MIN_POWER}-${NOOB_MIN_POWER})<<42" OUTPUT_FORMAT HEXADECIMAL)

set(NOOB_STACK_SIZE             0x400000ULL) # bzip2 needs large objects on the stack!

set(CHECK_DEREFERENCE_SITES     ON)
set(CHECK_ESCAPE_SITES          ON)
set(CHECK_POINTER_ARITHMETIC    ON)
set(CHECK_POINTER_DEREFERENCES  ON)
set(USE_BRANCHING_CHECKS        ON)
set(INTRAPROCEDURAL_ONLY        ON)
if (NOT INTRAPROCEDURAL_ONLY)
    set(SOUND_POINTER_DETECTION ON)     # Interprocedural analyses must be enabled to use sound pointer detection
endif()

set(MASK_EXT_PTR_ARGS           OFF)
if (NOT CHECK_POINTER_ARITHMETIC)
    set(COUNT_NOOB_FAILURES     OFF)     # Masked ptr counting only implemented for deref-only
endif()

# Whether stack allocations should be moved to individual N00B stacks and
# checked.
set(REPLACE_STACK_ALLOCS        OFF)
set(REMAP_GLOBALS               OFF)

set(EMIT_RUNTIME_CALLS          OFF)

set(EMULATE_LOWFAT              OFF)

# Extra padding that should be added to each allocation using the N00B
# allocator. Useful for testing.
set(NOOBALLOC_PADDING_BYTES     0)

# Show allocation metrics when a thread finishes or the allocator is unloaded.
# This only collects the metrics. Enable logging to actually show them. Handy
# for testing the impact of collecting metrics without any logging.
set(NOOBALLOC_METRICS           1)

# Whether useful log messages should be enabled.
set(DO_LOGGING                  1)

# Whether tracing of function calls should be enabled.
set(DO_TRACING                  0)
