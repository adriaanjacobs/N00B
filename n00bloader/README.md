# N00B loader

A small ELF chainloader based on [MikhailProg/elf](https://github.com/MikhailProg/elf), which reserves the N00B-managed virtual memory area up front before loading the rest of the program. 

## Build

```bash
cmake -B build/ .
cmake --build build/
```

## Run
To run explicitly, use:
```bash
build/n00bloader <exe>
```

`n00bloader` also supports being run as ELF interpreter. For that, use
```bash
patchelf --set-interpreter </path/to/n00bloader> <binary>
```
or use the `-Wl,-dynamic-linker,/path/to/n00bloader` command line option in your compiler. 
