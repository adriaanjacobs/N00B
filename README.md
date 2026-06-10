# The **N00B** Bounds Checker

This repo contains the compiler passes & runtime code necessary to harden applications with the N00B bounds checker. Clone it to get started:
```bash
git clone --recurse-submodules <url>

# or, if already cloned without submodule
git submodule init
git submodule update
```

## Citation
`N00B` started as a research project published at ACM CCS'26. Please use the following BibTex entry to cite it:
```bibtex
@inproceedings{jacobs2026n00b,
author = {Jacobs, Adriaan and Ramponi, Carlo and Roels, Jonas and Crispo, Bruno and Vlasceanu, Silviu and Ammar, Mahmoud and Volckaert, Stijn},
title = {{N00B}: Bounds Checking for the Masses},
year = {2026},
booktitle = {Proceedings of the 33rd ACM Conference on Computer and Communications Security (CCS'26)},
note = {To appear.}
}
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
cmake ../ -DLLVM_DIR=/usr/lib/llvm-15/lib/cmake/ -DClang_DIR=/usr/lib/llvm-15/lib/cmake/clang/
make -j
```
If you manually installed LLVM to a non-standard location, you can set the `LLVM_DIR` CMake variable to the `cmake/llvm` folder of your installation (containing `LLVMConfig.cmake`, `find <folder> -name "LLVMConfig.cmake"`). Same for `Clang_DIR`. 

## Hardening code with N00B
For implementation reasons, N00B currently only supports whole-program IR during Link Time Optimization. There are two main ways to achieve this. 

> _**NOTE**_: We are working to eliminate this unnecessary LTO requirement, and already provide alternatives for most most whole-program analyses in `N00B`. Reach out if you are interested in this! 

### (Recommended) Single-step process: integrate with build system (noobclang)
Use Link Time Optimization and integrate NOOB with the build settings of the project. 

We generate (best effort!) drop-in clang(++) replacer scripts called `noobclang(++)`, which apply the right flags during compilation and linking. 
You can simply specify `noobclang` as the project's compiler at configuration or build time. This will work for most projects, most of the time. 
```bash
CC=${NOOB_DIR}/build/noobclang 
CXX=${NOOB_DIR}/build/noobclang++
```

> _**NOTE**_: The script simply looks for a `-c` command line option to figure out whether it's being called at compile or link time. This is a very stupid heuristic, but works so far on all projects we have tested. If you encounter weird problems, it is likely due to this. Let us know!

If you want to test just the impact of `N00B`'s allocator, we also provide similar `nooballocclang(++)` scripts. These do not instrument the program, but only link in the runtime support. 

### (Manual) Two-step process: generate IR code first, then harden & compile down
With [WLLVM](https://github.com/travitch/whole-program-llvm) or [GLLVM](https://github.com/SRI-CSL/gllvm), you can generate the whole-program IR for software projects. Afterwards, use the `run_llvm_noob` executable to modify the IR. Finally, link in the N00B runtime.
```bash
# build project with gllvm
get-bc <exe name>                                   # get bitcode with gllvm
build/llvm-plugin/run_llvm_noob ir.ll ir.noob.ll    # modify IR to insert bounds checks
clang-15 ir.noob.ll \
        -fuse-ld=/usr/bin/ld.lld-15 -Xclang -no-opaque-pointers \
        -Wl,-rpath=${NOOB_DIR}/build/allocator/ ${NOOB_DIR}/build/allocator/libnooballoc.so \
        -Wl,-dynamic-linker,${NOOB_DIR}/n00bloader/n00bloader \
        -o ir.noob
./ir.noob <args>
```

This is mostly useful for testing small files or debugging the compiler given an IR dump. 
For convenience, you can also use `noobclang(++)` on C/C++/IR files, like a regular compiler, which simplifies such testing. Use as follows:
```bash
${NOOB_DIR}/build/noobclang test.[c/cpp/ll] -o test
```

### Benchmarking
We generate SPEC CPU2006 (`<build>/templates/spec06/`) and SPEC CPU2017 (`<build>/templates/spec17/`) configs for benchmarking N00B: 
* `clang15-lto-baseline.cfg` runs without any N00B changes, but with the same compiler and optimization options.
* `clang15-lto-nooballoc.cfg` measures the impact of N00B's memory layout requirements, with `nooballoc` linked into the binaries, and `n00bloader` to place them above `NOOB_MAX_ADDR`.  
* `clang15-lto-noob.cfg` runs with full N00B hardening enabled. 
