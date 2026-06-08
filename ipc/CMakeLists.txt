add_library(ipc STATIC)

target_sources(ipc
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/ipc_context.c
        ${CMAKE_CURRENT_LIST_DIR}/radio_state.c
)

target_include_directories(ipc
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(ipc
    PRIVATE
        utils
    PUBLIC
        freertos_kernel
)
