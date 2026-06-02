add_library(event_manager STATIC)

target_sources(event_manager
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/event_manager.c
)

target_include_directories(event_manager
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(event_manager
    PRIVATE
        freertos_kernel
        logger
        utils
        ipc
)
