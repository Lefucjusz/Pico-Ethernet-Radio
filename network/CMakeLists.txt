add_library(network STATIC)

target_sources(network
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/enc28j60/enc28j60.c
        ${CMAKE_CURRENT_LIST_DIR}/ethernetif/ethernetif.c
        ${CMAKE_CURRENT_LIST_DIR}/connection/connection.c
        ${CMAKE_CURRENT_LIST_DIR}/server/server.c
        ${CMAKE_CURRENT_LIST_DIR}/network.c
)

target_include_directories(network
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/enc28j60
        ${CMAKE_CURRENT_LIST_DIR}/ethernetif
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/connection
        ${CMAKE_CURRENT_LIST_DIR}/server
        ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(network
    PRIVATE
        hardware_spi
        freertos_kernel
        logger
        lwip
        utils
        gpio_irq
    PUBLIC
        ipc
)
