find_package(LLVM 15 REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# generate the noob linker script for the appropriate architecture
#   check the target arch
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
    set(LINKER_SCRIPT lld.x86-64.ld)
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    set(LINKER_SCRIPT lld.AArch64.ld)
else()
    message(FATAL_ERROR "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()
#   generate the linker script. It will automatically fill in the relevant variables
configure_file(
    ${LINKER_SCRIPT}
    ${CMAKE_BINARY_DIR}/linker_script/noob_linker_script.ld
)

set(DEBUGIR_LLVM_VERSION 15)
add_subdirectory(debugir/)

add_subdirectory(llvm-utils/)

# The below part can be moved into its own subdirectory if you want (I typically do it like that)
add_library(llvmnoob SHARED 
    llvm_noob.cpp 
    radix_decoder.cpp
    llvm_noob_plugin.cpp
)
target_compile_options(llvmnoob PUBLIC 
                            -std=c++20 -Wall -Wextra -Werror=return-type -Wimplicit-fallthrough -Wno-unused-parameter -Wno-unused-variable
                            -ggdb -fPIC -fno-rtti
                            -O2
                            # -fsanitize=address
)
# target_link_options(llvmnoob PUBLIC -fsanitize=address)
target_include_directories(llvmnoob PUBLIC 
    ${LLVM_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}/linker_script/
)
target_link_libraries(llvmnoob llvmutils dbgir)

# an executable that sets up LLVM's pass machinery and runs our pass on a provided bitcode file
add_executable(run_llvm_noob run_llvm_noob.cpp)
target_link_libraries(run_llvm_noob llvmnoob)
