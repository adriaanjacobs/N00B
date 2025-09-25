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
clang-15 ir.noob.ll -fuse-ld=lld-15 -mcmodel=large -no-pie -T noob_linker_script.ld -O2 -o ir.noob -Wl,-rpath=${NOOB_DIR}/build/allocator/ ${NOOB_DIR}/build/allocator/libnooballoc.so
./ir.noob <args>
``` 

### Single-step process: integrate with build system
Another way to handle all of this at once is to use Link Time Optimization and integrate NOOB with the build settings of the project. 
> **NOTE**: One complication here is that we generate a custom linker script during NOOB's instrumentation, so the linker script doesn't exist yet if we run NOOB at LTO. We work around this by running the LTO stage twice. 

This means passing `-flto -Xclang -no-opaque-pointers` to `CFLAGS` and using `noobclang` or `noobclang++` as `CLD`. Here are the relevant settings for SPEC CPU2006 that we use:
```bash
CC           = clang-15 -Xclang -no-opaque-pointers -flto 
CLD          = NOOB_BUILD_DIR=${NOOB_DIR}/build ${NOOB_DIR}/noobclang 
CXX          = clang++-15 -Xclang -no-opaque-pointers -flto 
CXXLD        = NOOB_BUILD_DIR=${NOOB_DIR}/build ${NOOB_DIR}/noobclang++ 
FC           = we-do-not-support-fortran
```

## Running N00B-hardened code
Sometimes, depending on allocator settings, we need more than 65K mappings (`vm.max_map_count`). Specifically, SPEC06's `dealII` and `sphinx3` seem to require it. 
```bash
# use sysctl -p to make this persist across reboots
sudo sysctl vm.max_map_count=262144
```
