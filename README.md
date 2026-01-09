# The **N00B** Bounds Checker

This repo contains the compiler passes & runtime code necessary to harden applications with the N00B bounds checker. Clone it to get started:
```bash
git clone --recurse-submodules <url>

# or, if already cloned without submodule
git submodule init
git submodule update
```

## Building
N00B is made up of out-of-tree compiler passes for LLVM 15. For Ubuntu 20.04 and 22.04 you can grab pre-built LLVM libraries from [LLVM's apt repositories](https://apt.llvm.org/). General instructions like so:
```bash
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 15
sudo apt install libclang-15-dev # not installed by default
```
On Ubuntu 24.04, you can install them directly from Ubuntu's repos instead:
```bash
sudo apt install clang-15 llvm-15 libclang-15-dev lld-15
```

Then, run `cmake` from a build directory to build N00B.
```bash
mkdir build
cd build
cmake ../ # Optional: -DLLVM_DIR=/usr/lib/llvm-15/lib/cmake/ -DClang_DIR=/usr/lib/llvm-15/lib/cmake/clang/
make -j
```
If you manually installed LLVM to a non-standard location, you can set the `LLVM_DIR` CMake variable to the `cmake/llvm` folder of your installation (containing `LLVMConfig.cmake`, `find <folder> -name "LLVMConfig.cmake"`). Same for `Clang_DIR`. 


## Hardening code with N00B
N00B expects whole-program LLVM IR code to perform its analyses and transformations. There are two main ways to do this.

### Two-step process: generate IR code first, then harden & compile down
With [WLLVM](https://github.com/travitch/whole-program-llvm) or [GLLVM](https://github.com/SRI-CSL/gllvm), you can generate the whole-program IR for software projects. Afterwards, use the `run_llvm_noob` executable to modify the IR. Finally, link in the N00B runtime.
```bash
# build project with gllvm
get-bc <exe name>                                   # get bitcode with gllvm
build/llvm-plugin/run_llvm_noob ir.ll ir.noob.ll    # modify IR to insert bounds checks
clang-15 ir.noob.ll \
        -fuse-ld=/usr/bin/ld.lld-15 -Xclang -no-opaque-pointers \
        -Wl,-rpath=${NOOB_DIR}/build/allocator/ ${NOOB_DIR}/build/allocator/libnooballoc.so \
        -Wl,-dynamic-linker,${NOOB_DIR}/n00bloader/n00bloader
./ir.noob <args>
```

For convenience, N00B generates a `noobclang` and `noobclang++` script in the root of the build folder that accepts C/C++/IR code and passes these options already. 

### Single-step process: integrate with build system (noobclang)
Another way to handle all of this at once is to use Link Time Optimization and integrate NOOB with the build settings of the project. 

This means passing `-flto -Xclang -no-opaque-pointers` to `CFLAGS` and using `noobclang` or `noobclang++` as `C(XX)LD`. Here are the relevant settings for SPEC CPU2006 that we use:
```bash
CC           = clang-15 -Xclang -no-opaque-pointers -flto 
CLD          = ${NOOB_DIR}/build/noobclang 
CXX          = clang++-15 -Xclang -no-opaque-pointers -flto 
CXXLD        = ${NOOB_DIR}/build/noobclang++ 
FC           = we-do-not-support-fortran
```
