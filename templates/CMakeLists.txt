# generate config.h include file
configure_file(
    config.h.in
    ${CMAKE_BINARY_DIR}/include/NOOB/config.h
)

# generate noobclang(++)
configure_file(
    noobclang
    ${CMAKE_BINARY_DIR}/noobclang
    @ONLY
)
file(CREATE_LINK ${CMAKE_BINARY_DIR}/noobclang ${CMAKE_BINARY_DIR}/noobclang++ SYMBOLIC)

# generate SPEC config files
## first baseline
set(NOOB_CLD "clang-15")
set(NOOB_CXXLD "clang++-15")
set(SPEC_EXT "baseline")
configure_file(
    spec06-clang15-lto-${CMAKE_SYSTEM_PROCESSOR}.cfg
    ${CMAKE_CURRENT_BINARY_DIR}/spec06/clang15-lto-baseline.cfg
)
configure_file(
    spec17-clang15-lto-${CMAKE_SYSTEM_PROCESSOR}.cfg
    ${CMAKE_CURRENT_BINARY_DIR}/spec17/clang15-lto-baseline.cfg
)

## then nooballoc
set(NOOB_CLD "${NOOB_CLD} -Wl,-rpath=${CMAKE_BINARY_DIR}/allocator/ ${CMAKE_BINARY_DIR}/allocator/libnooballoc.so -Wl,-dynamic-linker,${CMAKE_BINARY_DIR}/n00bloader/n00bloader")
set(NOOB_CXXLD "${NOOB_CXXLD} -Wl,-rpath=${CMAKE_BINARY_DIR}/allocator/ ${CMAKE_BINARY_DIR}/allocator/libnooballoc.so -Wl,-dynamic-linker,${CMAKE_BINARY_DIR}/n00bloader/n00bloader")
set(SPEC_EXT "nooballoc")
set(NOOB_STRIPTOP "-DNOOB_STRIPTOP")
configure_file(
    spec06-clang15-lto-${CMAKE_SYSTEM_PROCESSOR}.cfg
    ${CMAKE_CURRENT_BINARY_DIR}/spec06/clang15-lto-nooballoc.cfg
)
configure_file(
    spec17-clang15-lto-${CMAKE_SYSTEM_PROCESSOR}.cfg
    ${CMAKE_CURRENT_BINARY_DIR}/spec17/clang15-lto-nooballoc.cfg
)

## then full noob
set(NOOB_CLD "${CMAKE_BINARY_DIR}/noobclang")
set(NOOB_CXXLD "${NOOB_CLD}++")
set(SPEC_EXT "noob")
configure_file(
    spec06-clang15-lto-${CMAKE_SYSTEM_PROCESSOR}.cfg
    ${CMAKE_CURRENT_BINARY_DIR}/spec06/clang15-lto-noob.cfg
)
configure_file(
    spec17-clang15-lto-${CMAKE_SYSTEM_PROCESSOR}.cfg
    ${CMAKE_CURRENT_BINARY_DIR}/spec17/clang15-lto-noob.cfg
)
