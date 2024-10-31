#pragma once

#define TAG_WIDTH                   (8)
#define NOOB_STACK_SIZE             (4ULL * 1024 * 1024) // bzip2 needs large objects on the stack!

#define TAG_POINTERS                1
#define CHECK_POINTER_DEREFERENCES  1
#define CHECK_POINTER_ARITHMETIC    1
#define REPLACE_STACK_ALLOCS        1
#define REMAP_GLOBAS                1
