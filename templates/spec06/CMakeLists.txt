# generate SPEC CPU2006 config files
set(NOOB_CLD "clang-15 -flto -fuse-ld=/usr/bin/ld.lld-15 -Xclang -disable-O0-optnone -Wl,-plugin-opt=no-opaque-pointers")
set(NOOB_CXXLD "clang++-15 -flto -fuse-ld=/usr/bin/ld.lld-15 -Xclang -disable-O0-optnone -Wl,-plugin-opt=no-opaque-pointers")
set(SPEC_EXT "baseline")
configure_file(
    clang15-lto-${CMAKE_SYSTEM_PROCESSOR}.cfg
    ${CMAKE_CURRENT_BINARY_DIR}/clang15-lto-baseline.cfg
)

set(NOOB_CLD "${NOOB_CLD} -Wl,-rpath=${CMAKE_BINARY_DIR}/allocator/ ${CMAKE_BINARY_DIR}/allocator/libnooballoc.so -Wl,-dynamic-linker,${CMAKE_BINARY_DIR}/n00bloader/n00bloader")
set(NOOB_CXXLD "${NOOB_CXXLD} -Wl,-rpath=${CMAKE_BINARY_DIR}/allocator/ ${CMAKE_BINARY_DIR}/allocator/libnooballoc.so -Wl,-dynamic-linker,${CMAKE_BINARY_DIR}/n00bloader/n00bloader")
set(SPEC_EXT "nooballoc")
set(NOOB_STRIPTOP "-DNOOB_STRIPTOP")
configure_file(
    clang15-lto-${CMAKE_SYSTEM_PROCESSOR}.cfg
    ${CMAKE_CURRENT_BINARY_DIR}/clang15-lto-nooballoc.cfg
)

set(NOOB_CLD "${CMAKE_BINARY_DIR}/noobclang")
set(NOOB_CXXLD "${NOOB_CLD}++")
set(SPEC_EXT "noob")
configure_file(
    clang15-lto-${CMAKE_SYSTEM_PROCESSOR}.cfg
    ${CMAKE_CURRENT_BINARY_DIR}/clang15-lto-noob.cfg
)
