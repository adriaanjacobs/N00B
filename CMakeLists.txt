cmake_minimum_required(VERSION 3.5) # this can probably be relaxed tbh
project(NOOB)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(llvm-plugin)
