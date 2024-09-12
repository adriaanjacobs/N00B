#pragma once

#include <stddef.h>

#define TAG_WIDTH (8)

void noob_init(size_t max_radix);

void* noob_allocate(size_t nbytes);

void noob_free(void* ptr);
