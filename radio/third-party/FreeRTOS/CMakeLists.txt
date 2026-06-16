add_library(freertos_config INTERFACE)

target_include_directories(freertos_config SYSTEM
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(freertos_config
    INTERFACE
        projCOVERAGE_TEST=0
)

set(FREERTOS_PORT GCC_RP2040 CACHE STRING "" FORCE)
set(FREERTOS_HEAP "4" CACHE STRING "" FORCE)

add_subdirectory(FreeRTOS-Kernel)
