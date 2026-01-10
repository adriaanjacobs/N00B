cmake_minimum_required(VERSION 3.5) # this can probably be relaxed tbh

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_COMPILER clang-15)
set(CMAKE_CXX_COMPILER clang++-15)

project(NOOB)

# include NOOB configuration variables
include(noobconfig.cmake)

add_subdirectory(n00bloader)

# generate a bunch of scripts & utilities
add_subdirectory(templates)

include_directories(${CMAKE_BINARY_DIR}/include/)
include_directories(include)

add_subdirectory(llvm-plugin)
add_subdirectory(allocator)
