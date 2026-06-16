add_library(fota STATIC)

target_sources(fota
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/fota.c
)

target_include_directories(fota
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(fota
    PRIVATE
        pico_flash
        hardware_flash
        utils
        logger
        common
)
