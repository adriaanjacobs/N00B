project(noob-allocator)
cmake_minimum_required(VERSION 3.5) # this can probably be relaxed tbh

add_library(nooballoc SHARED nooballoc.cpp malloc_wrapper.cpp)
target_compile_options(nooballoc PUBLIC 
    -Wall -Wextra -Werror=return-type -Wimplicit-fallthrough -Wno-unused-parameter -Wno-unused-variable
    -O2 -std=c++20 -ggdb
)
target_compile_options(nooballoc PRIVATE -fno-rtti -fno-exceptions)
target_compile_options(nooballoc PRIVATE -flto)
if(EMIT_RUNTIME_CALLS)
    target_compile_options(nooballoc PRIVATE -O1)
endif()
target_link_options(nooballoc PRIVATE -flto -fuse-ld=lld-15)
target_link_libraries(nooballoc "-ldl")

add_executable(test_noob test.cpp)
target_compile_options(test_noob PRIVATE
    -fno-builtin -fno-builtin-function
)
