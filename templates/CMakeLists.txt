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

add_subdirectory(spec06)
