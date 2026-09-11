cmake_minimum_required(VERSION 3.5) # this can probably be relaxed tbh

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_COMPILER clang-15)
set(CMAKE_CXX_COMPILER clang++-15)
set(NOOB_CLANG_TIDY_GLOBAL_CHECKS -*,bugprone-*,readability-*,-readability-magic-numbers,performance-*,modernize-*,-modernize-use-auto,misc-*,cppcoreguidelines-pro-type-cstyle-cast,-bugprone-easily-swappable-parameters,-performance-no-int-to-ptr,-bugprone-unchecked-optional-access)

project(NOOB)

# Clang-Tidy 17 is the first version with the `misc-include-cleaner` check, which is a really
# convenient one, especially to remove unused headers.
find_program(NOOB_CLANG_TIDY clang-tidy-17)
if(NOOB_CLANG_TIDY STREQUAL "NOOB_CLANG_TIDY-NOTFOUND")
    message(WARNING "clang-tidy not found: C/C++ linting disabled")
endif()

# include NOOB configuration variables
include(noobconfig.cmake)

add_subdirectory(n00bloader)

# generate a bunch of scripts & utilities
add_subdirectory(templates)

include_directories(${CMAKE_BINARY_DIR}/include/)
include_directories(include)

add_subdirectory(llvm-plugin)
add_subdirectory(allocator)
