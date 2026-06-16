cmake_minimum_required(VERSION 3.13)

add_executable(bootloader main.c)

pico_set_program_name(bootloader "bootloader")
pico_set_program_version(bootloader "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(bootloader 1)
pico_enable_stdio_usb(bootloader 0)

add_subdirectory(crc32)
add_subdirectory(image)
add_subdirectory(update)

target_link_options(bootloader
    PRIVATE
        -Wl,--print-memory-usage
)

target_link_libraries(bootloader
        pico_stdlib
        image
        update
)

# Add the standard include files to the build
target_include_directories(bootloader
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

pico_set_linker_script(bootloader ${CMAKE_CURRENT_LIST_DIR}/bootloader_memmap.ld)

pico_add_extra_outputs(bootloader)
