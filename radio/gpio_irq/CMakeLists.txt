add_library(gpio_irq STATIC)

target_sources(gpio_irq
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/gpio_irq.c
)

target_include_directories(gpio_irq
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(gpio_irq
    PRIVATE
        pico_stdlib
)
