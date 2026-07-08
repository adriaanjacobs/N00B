# The N00B Bounds Checker

This repo contains the compiler passes & runtime code necessary to harden applications with the N00B bounds checker. 
Clone everything to get started:
```bash
git clone --recurse-submodules <url>

# or, if already cloned without submodule
git submodule init
git submodule update
```

## Citation
N00B started as a [research project](https://adriaanjacobs.github.io/files/ccs26n00b.pdf) published at [ACM CCS'26](https://www.sigsac.org/ccs/CCS2026/). Please use the following BibTex entry to cite it:
```bibtex
@inproceedings{jacobs2026n00b,
  author = {Jacobs, Adriaan and Ramponi, Carlo and Roels, Jonas and Crispo, Bruno and Vlasceanu, Silviu and Ammar, Mahmoud and Volckaert, Stijn},
  title = {{N00B}: Bounds Checking for the Masses},
  year = {2026},
  booktitle = {Proceedings of the 33rd ACM Conference on Computer and Communications Security (CCS'26)},
  note = {To appear.}
}
```

## Code Structure
N00B's implementation consists of three main parts:
1. [**`llvm-plugin/`**](/llvm-plugin/): an LTO plugin for the LLVM compiler that inserts bounds checks into C/C++ programs during compilation
2. [**`allocator/`**](/allocator/): a custom dynamic memory allocator that arranges stack and heap objects such that they satisfy N00B's memory layout.
3. [**`n00bloader/`**](/n00bloader/): a small program loader (chainloader) that reserves the N00B-managed memory region up front, so that non-N00B pointers can efficiently be ignored by the bounds checks

Most of N00B's compiler optimizations are implemented in the reusable [CoROLLA](https://github.com/adriaanjacobs/CoROLLA) library, submoduled at **`llvm-plugin/corolla`**. 

## Requirements
N00B relies on hardware **pointer masking** to make tagged pointers natively dereferenceable, and kernel support to manage and expose that hardware functionality to user-space processes. 
Below is the current status of that support, as of July 2026: 

| Arch | Extension | Hardware | Kernel |
| --- | --- | --- | --- |
| `aarch64` | Arm **TBI** (Top-Byte Ignore) | Any 64-bit Arm CPU | Tagged-address ABI (`PR_SET_TAGGED_ADDR_CTRL`), Linux ≥ 5.4 |
| `x86_64` (AMD) | AMD **UAI** (Upper Address Ignore) | Zen 4 or newer | [**Not yet in mainline**](https://www.phoronix.com/news/AMD-Linux-UAI-Zen-4-Tagging) |
| `x86_64` (Intel) | Intel **LAM** (Linear Address Masking), `U48` variant | Core Ultra 200 (Arrow Lake / Lunar Lake) or newer | **Not yet in mainline**, out-of-tree LAM U48 patches required ([details](/docs/intel-lam.md)) |

> [!NOTE]
> In principle, N00B could run on LAM U57 as well, which is already supported in the mainline kernel (but see [docs/intel-lam.md](docs/intel-lam.md)). 
> We have not yet tested this configuration ourselves, but we would be happy to help make it work: if you are interested in a U57 port, please get in touch!

`noobconfig.cmake` auto-detects the host at configuration time and picks the matching tag layout; there is nothing to set by hand on a supported platform. 

### Compat mode (unsupported hardware)
If your platform lacks a pointer masking extension, you can still build and run N00B in a non-secure **compat mode** to obtain performance numbers. In compat mode, bounds checks are compiled to no-ops and no pointers are tagged, so both in-bounds and out-of-bounds accesses complete without trapping.

To enable it, set the following two variables at the bottom of [`noobconfig.cmake`](/noobconfig.cmake) before configuring:
```cmake
set(NOOB_IGNORE_ERRORS      ON)     # decode all N00B radices to a no-op value
set(NOOB_TAG_POINTERS       OFF)    # skip enabling LAM/TBI at run time
```
With these set, N00B can build and run programs (insecurely) on any x86_64/AArch64 host, regardless of LAM/TBI support.

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

> [!NOTE]
> We are working to eliminate this unnecessary LTO requirement, and already provide alternatives for most whole-program analyses in N00B. Reach out if you are interested in this! 

### (Recommended) Single-step process: integrate with build system (`noobclang`)
Use Link Time Optimization and integrate N00B with the build settings of the project. 

We provide drop-in clang(++) replacer scripts called `noobclang(++)`, which apply the right flags during compilation and linking. 
You can simply specify `noobclang` as the project's compiler at configuration or build time. This will work for most projects, most of the time. 
```bash
CC=${NOOB_DIR}/build/noobclang 
CXX=${NOOB_DIR}/build/noobclang++
```

> [!NOTE]
> The script simply looks for a `-c` command line option to figure out whether it's being called at compile or link time. This is a very stupid heuristic, but works so far on all projects we have tested. If you encounter weird problems, it is likely due to this. Let us know!

If you want to test just the impact of N00B's allocator, we also provide similar `nooballocclang(++)` scripts. These do not instrument the program, but only link in the runtime support. 

### (Manual) Two-step process: generate IR code first, then harden & compile down
With [WLLVM](https://github.com/travitch/whole-program-llvm) or [GLLVM](https://github.com/SRI-CSL/gllvm), you can generate the whole-program IR for software projects. Afterwards, use the `run_llvm_noob` executable to modify the IR. Finally, link in the N00B runtime.
```bash
# build project with gllvm
get-bc <exe name>                                   # get bitcode with gllvm
${NOOB_DIR}/build/llvm-plugin/run_llvm_noob ir.ll ir.noob.ll    # modify IR to insert bounds checks
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
We generate SPEC CPU2006 (`${NOOB_DIR}/<build>/templates/spec06/`) and SPEC CPU2017 (`${NOOB_DIR}/<build>/templates/spec17/`) configs for benchmarking N00B: 
* `clang15-lto-baseline.cfg` runs without any N00B changes, but with the same compiler and optimization options.
* `clang15-lto-nooballoc.cfg` measures the impact of N00B's memory layout requirements, with `nooballoc` linked into the binaries, and `n00bloader` to place them above `NOOB_MAX_ADDR`.  
* `clang15-lto-noob.cfg` runs with full N00B hardening enabled. 

All of these are generated based on your specific build environment, and can be dropped in to SPEC's config folder without any modification. 

As reported in the paper, we applied some patches to GCC in both editions of SPEC. You can find those under [patches/](/patches/).

## Smoke Test
We provide a simple [test/test.c](/test/test.c) file, which you may compile down using `noobclang` and use to test out N00B's functionality. 
Please note that you'll need a platform supporting pointer masking to do this (see [Requirements](#requirements)). 

