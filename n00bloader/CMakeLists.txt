cmake_minimum_required(VERSION 3.13)

project(n00bloader C ASM)

# Automatically detect architecture (amd64 or aarch64)
if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "aarch64")
    set(DEFAULT_ARCH "aarch64")
else()
    set(DEFAULT_ARCH "amd64")
endif()

set(ARCH "${DEFAULT_ARCH}" CACHE STRING "Target architecture")

file(GLOB SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
file(GLOB ASM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/${ARCH}/*.S")
add_executable(n00bloader 
    ${SOURCES}
    ${ASM_SOURCES}
)

# Force 64-bit mode
target_compile_definitions(n00bloader PRIVATE ELFCLASS=ELFCLASS64)

target_compile_options(n00bloader PRIVATE
    -Wall -Wextra -pedantic
    -fno-stack-protector
    -ggdb
    -fvisibility=hidden
    -fno-asynchronous-unwind-tables
    -fno-unwind-tables
)

target_link_options(n00bloader PRIVATE 
    -static-pie
    -nostartfiles
    -nodefaultlibs
    -nostdlib
    "LINKER:-e,z_start"
)

if (NOOB_MAX_ADDR)
    target_link_options(n00bloader PRIVATE "LINKER:-Ttext-segment=${NOOB_MAX_ADDR}")
    target_compile_definitions(n00bloader PRIVATE NOOB_MAX_ADDR=${NOOB_MAX_ADDR})
endif()
