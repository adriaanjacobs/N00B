project(noob-allocator)
cmake_minimum_required(VERSION 3.5) # this can probably be relaxed tbh

set(COMMON_CXX_OPTIONS
    -std=c++20 -Wall -Wextra -Werror=return-type -Wimplicit-fallthrough -Wno-unused-parameter -Wno-unused-variable
    -ggdb -fPIC -fno-rtti -fno-exceptions
    -O2
)

add_library(nooballoc SHARED nooballoc.cpp malloc_wrapper.cpp)
target_compile_options(nooballoc PUBLIC ${COMMON_CXX_OPTIONS})

add_executable(test_noob test.cpp)
