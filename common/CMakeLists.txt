add_library(common INTERFACE)

target_include_directories(common
    INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(common
    INTERFACE
        hardware_flash
)
