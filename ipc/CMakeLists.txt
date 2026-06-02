add_library(ipc STATIC)

target_sources(ipc
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/ipc_context.c
)

target_include_directories(ipc
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(ipc
    PUBLIC
        freertos_kernel
)
