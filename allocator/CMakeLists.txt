project(noob-allocator)
cmake_minimum_required(VERSION 3.5) # this can probably be relaxed tbh

add_library(nooballoc nooballoc.cpp)
target_compile_options(nooballoc PUBLIC
    -std=c++20 -Wall -Wextra -Werror=return-type -Wimplicit-fallthrough -Wno-unused-parameter -Wno-unused-variable
    -ggdb -fPIC -fno-rtti
    -O2
)

add_executable(test_noob test.cpp)
target_link_libraries(test_noob nooballoc)
