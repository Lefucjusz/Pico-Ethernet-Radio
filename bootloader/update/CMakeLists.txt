add_library(update STATIC)

target_sources(update
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/update.c
)

target_include_directories(update
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(update
    PRIVATE
        image
        common
)
