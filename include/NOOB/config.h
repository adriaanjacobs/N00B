#pragma once

#define TAG_WIDTH                   (8)

#define TAG_POINTERS                1
#define CHECK_POINTER_DEREFERENCES  (1 && TAG_POINTERS)
#define CHECK_POINTER_ARITHMETIC    1
#define REPLACE_STACK_ALLOCS        1
#define REMAP_GLOBAS                1
