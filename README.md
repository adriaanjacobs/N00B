# The **NOOB** Bounds Checker

```bash
cmake  ../ -DCMAKE_CXX_COMPILER=clang++-15 -DCMAKE_C_COMPILER=clang-15 -DLLVM_DIR=/usr/lib/llvm-15/lib/cmake/ -DClang_DIR=/usr/lib/llvm-15/lib/cmake/clang/
make -j
```

Sometimes, we need more than 65K mappings (`vm.max_map_count`). Specifically, SPEC06's `dealII` and `sphinx3` seem to require it. 
```bash
# use sysctl -p to make this persist across reboots
sudo sysctl vm.max_map_count=262144
```
