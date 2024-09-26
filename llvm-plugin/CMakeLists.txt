find_package(LLVM 15 REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

add_subdirectory(llvm-utils/)

# The below part can be moved into its own subdirectory if you want (I typically do it like that)
add_library(llvmnoob SHARED llvm_noob.cpp llvm_noob_plugin.cpp)
target_compile_options(llvmnoob PUBLIC 
                            -std=c++20 -Wall -Wextra -Werror=return-type -Wimplicit-fallthrough -Wno-unused-parameter -Wno-unused-variable
                            -ggdb -fPIC -fno-rtti
                            -O2
)
target_include_directories(llvmnoob PUBLIC 
    ${LLVM_INCLUDE_DIRS}
    llvm-utils/include/
)
target_link_libraries(llvmnoob llvmutils)

# an executable that sets up LLVM's pass machinery and runs our pass on a provided bitcode file
add_executable(run_llvm_noob run_llvm_noob.cpp)
target_link_libraries(run_llvm_noob llvmnoob)
