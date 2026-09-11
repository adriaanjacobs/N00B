cmake_minimum_required(VERSION 3.5) # this can probably be relaxed tbh

project(noob-allocator)

add_library(nooballoc SHARED
    arena.cpp
    init.cpp
    libc_alloc.cpp
    noob_alloc.cpp
    log.cpp
    util.cpp
    size_region_allocator.cpp
    thread_allocator.cpp
    thread_size_allocator.cpp
)
set_property(TARGET nooballoc PROPERTY CXX_VISIBILITY_PRESET hidden)
target_compile_options(nooballoc PUBLIC 
    -Wall -Wextra -Werror=return-type -Wimplicit-fallthrough -Wno-unused-parameter -Wno-unused-variable
    -std=c++20 -ggdb
)
target_compile_options(nooballoc PRIVATE -fno-rtti -fno-exceptions)
target_compile_options(nooballoc PRIVATE -flto)
if(EMIT_RUNTIME_CALLS)
    target_compile_options(nooballoc PRIVATE -O1 -fno-omit-frame-pointer)
endif()
target_link_options(nooballoc PRIVATE -flto)
target_link_options(nooballoc PRIVATE -fuse-ld=lld-15)
target_link_libraries(nooballoc "-ldl")
if(NOT NOOB_CLANG_TIDY STREQUAL "NOOB_CLANG_TIDY-NOTFOUND")
    set_property(TARGET nooballoc PROPERTY CXX_CLANG_TIDY ${NOOB_CLANG_TIDY} -checks=${NOOB_CLANG_TIDY_GLOBAL_CHECKS},-bugprone-reserved-identifier)
endif()
