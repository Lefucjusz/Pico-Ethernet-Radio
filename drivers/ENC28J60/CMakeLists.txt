add_library(ENC28J60 STATIC)

target_sources(ENC28J60
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/enc28j60.c
)

target_include_directories(ENC28J60
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(ENC28J60
    PRIVATE
        pico_stdlib
        hardware_spi
        freertos_kernel
)
