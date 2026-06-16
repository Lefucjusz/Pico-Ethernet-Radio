add_library(image STATIC)

target_sources(image
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/image.c
)

target_include_directories(image
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(image
    PRIVATE
        pico_flash
        crc32
        common
)
