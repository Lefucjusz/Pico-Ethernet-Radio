add_library(logger STATIC)

target_sources(logger
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/logger.c
)

target_include_directories(logger
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(logger
    PRIVATE
        pico_stdlib
        freertos_kernel
)
