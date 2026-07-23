# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.0)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(Pico-Ethernet-Radio C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

add_subdirectory(bootloader)
add_subdirectory(common)
add_subdirectory(radio)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Create factory image
set(FACTORY_BIN ${CMAKE_BINARY_DIR}/factory.bin)
set(FACTORY_UF2 ${CMAKE_BINARY_DIR}/factory.uf2)
set(RADIO_UPDATE_BIN ${CMAKE_BINARY_DIR}/radio/radio_update.bin)

add_custom_command(
    OUTPUT
        ${FACTORY_BIN}
        ${FACTORY_UF2}

    COMMAND ${CMAKE_COMMAND} -E echo "Creating factory image"
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/scripts/create_factory_image.py
            ${CMAKE_BINARY_DIR}/bootloader/bootloader.bin
            ${RADIO_UPDATE_BIN}
            ${FACTORY_BIN}
            ${FACTORY_UF2}

    DEPENDS
        bootloader
        ${RADIO_UPDATE_BIN}
)

add_custom_target(factory_image ALL
    DEPENDS
        ${FACTORY_UF2}
)
