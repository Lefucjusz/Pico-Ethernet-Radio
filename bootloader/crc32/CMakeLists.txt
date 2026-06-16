add_library(crc32 STATIC)

target_sources(crc32
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/crc32.c
)

target_include_directories(crc32
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
)
