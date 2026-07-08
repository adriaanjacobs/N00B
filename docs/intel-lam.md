# Intel LAM support

N00B's current Intel prototype is built on top of the **`U48`** variant of Intel's Linear Address Masking (LAM), which ignores 15 bits of every pointer (`ptr[62:48]`). 
N00B uses only 8 of those bits by default. 

The LAMU48 choice comes with a few considerations. 

## LAM U48 is not in the mainline kernel
Mainline Linux only implements the `U57` LAM variant (6 ignored bits, `ptr[62:57]`). 
Enforcing N00B's checks on Intel therefore requires the out-of-tree LAM U48 patches from Intel's [`kas/linux.git`](https://git.kernel.org/pub/scm/linux/kernel/git/kas/linux.git/log/?h=lam-old) tree (the `lam-old` branch; the paper builds on commit [`e64abde4`](https://git.kernel.org/pub/scm/linux/kernel/git/kas/linux.git/commit/?h=lam-old&id=e64abde4d7ab69eee2b4d11651d989c451930292)). 
The paper's Intel evaluation platform runs a custom **Linux 6.1.0-rc3** built from these patches.

It is currently [unclear](https://lwn.net/Articles/902094/) whether LAMU48 support will ever land in mainline Linux. 

## Even mainline U57 LAM is off by default
Due to the [SLAM](https://www.vusec.net/projects/slam/) attack, `CONFIG_ADDRESS_MASKING` is compiled out since Linux 6.12 unless kernel speculation mitigations are disabled (see [CVE-2024-50112](https://nvd.nist.gov/vuln/detail/CVE-2024-50112)). 
A stock distribution kernel therefore can not support N00B's checks on Intel, even for `U57`.

A hardware fix for SLAM exists, called **Linear Address Space Separation (LASS)**, which is already present on [Arrow Lake / Lunar Lake](https://edc.intel.com/content/www/us/en/design/products/platforms/details/arrow-lake-s/core-ultra-200s-series-processors-datasheet-volume-1-of-2/linear-address-space-separation-lass/) and Xeon 6 processors. 
[Kernel support](https://lwn.net/Articles/1044249/) to enable it is expected to [land soon](https://www.phoronix.com/news/Intel-LASS-v10), after which LAMU57 is likely to be re-enabled by default.

## Building against the LAM headers
N00B's allocator includes `<asm/prctl.h>` for the LAM `arch_prctl` constants (`ARCH_GET_UNTAG_MASK`, `ARCH_ENABLE_TAGGED_ADDR`, `ARCH_GET_MAX_TAG_BITS`). 

On platforms with outdated headers (but supported kernels), you can define them at configuration time to proceed:
```bash
cmake ../ <options> \
    -DCMAKE_CXX_FLAGS="-DARCH_GET_UNTAG_MASK=0x4001 -DARCH_ENABLE_TAGGED_ADDR=0x4002 -DARCH_GET_MAX_TAG_BITS=0x4003"
```

> [!NOTE]
> Defining these constants only lets the code *build*; enforcement still needs a kernel that supports LAM U48 at run time. 
> On an unsupported kernel, a hardened binary will abort at startup with `ARCH_GET_MAX_TAG_BITS: Invalid argument` if the kernel has no LAM at all, or `Please enable LAM U48 support on this kernel` if it only offers U57. On such hosts, use [compat mode](../README.md#compat-mode-unsupported-hardware) instead.
