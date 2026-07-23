cmake_minimum_required(VERSION 3.13)

add_executable(radio 
    main.c
    freertos_hooks.c # TODO move this somewhere else
)

pico_set_program_name(radio "radio")
pico_set_program_version(radio "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(radio 1)
pico_enable_stdio_usb(radio 0)

add_subdirectory(audio)
add_subdirectory(event_manager)
add_subdirectory(fota)
add_subdirectory(gpio_irq)
add_subdirectory(ipc)
add_subdirectory(logger)
add_subdirectory(network)
add_subdirectory(utils)
add_subdirectory(third-party/FreeRTOS)
add_subdirectory(third-party/lwip)
add_subdirectory(third-party/Helix-MP3-Decoder)

target_link_libraries(radio
        pico_stdlib
        pico_multicore
        freertos_kernel
        ipc
        network
        audio
        event_manager
        gpio_irq
        logger
        utils
)

target_link_options(radio
    PRIVATE
        -Wl,--print-memory-usage
)

# Add the standard include files to the build
target_include_directories(radio 
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

target_compile_definitions(radio 
    PRIVATE
        PICO_FLASH_SAFE_EXECUTE_SUPPORT_FREERTOS_SMP=1
)

pico_set_linker_script(radio ${CMAKE_CURRENT_LIST_DIR}/radio_memmap.ld)

pico_add_extra_outputs(radio)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Create update binary
set(RADIO_BIN ${CMAKE_CURRENT_BINARY_DIR}/radio.bin)
set(RADIO_UPDATE_BIN ${CMAKE_CURRENT_BINARY_DIR}/radio_update.bin)

add_custom_command(
    OUTPUT ${RADIO_UPDATE_BIN}

    COMMAND ${CMAKE_COMMAND} -E echo "Creating update binary"
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/scripts/add_header.py
            ${RADIO_BIN}
            ${RADIO_UPDATE_BIN}

    DEPENDS
        radio
        ${RADIO_BIN}
)

add_custom_target(radio_update ALL
    DEPENDS
        ${RADIO_UPDATE_BIN}
)
