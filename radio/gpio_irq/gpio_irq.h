#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <pico/types.h>

typedef void (*gpio_callback_t)(uint gpio, uint32_t events, void *arg);

void gpio_irq_init(void);

bool gpio_irq_register(uint gpio, uint32_t event_mask, gpio_callback_t callback, void *arg);
bool gpio_irq_unregister(uint gpio);
