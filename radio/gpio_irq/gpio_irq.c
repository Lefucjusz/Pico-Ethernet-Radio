#include "gpio_irq.h"
#include <hardware/gpio.h>

#define GPIO_IRQ_MAX_GPIOS 30

typedef struct
{
    uint32_t event_mask;
    gpio_callback_t callback;
    void *arg;
} gpio_irq_entry_t;

static gpio_irq_entry_t gpio_irq_table[GPIO_IRQ_MAX_GPIOS];

static void gpio_irq_dispatcher(uint gpio, uint32_t events)
{
    if (gpio >= GPIO_IRQ_MAX_GPIOS) {
        return;
    }

    const gpio_irq_entry_t *entry = &gpio_irq_table[gpio];

    if (entry->callback && ((events & entry->event_mask) != 0)) {
        entry->callback(gpio, events, entry->arg);
    }
}

void gpio_irq_init(void)
{
    gpio_set_irq_callback(gpio_irq_dispatcher);
    irq_set_enabled(IO_IRQ_BANK0, true);
}

bool gpio_irq_register(uint gpio, uint32_t event_mask, gpio_callback_t callback, void *arg)
{
    if ((gpio >= GPIO_IRQ_MAX_GPIOS) || (callback == NULL)) {
        return false;
    }

    gpio_irq_table[gpio].event_mask = event_mask;
    gpio_irq_table[gpio].callback = callback;
    gpio_irq_table[gpio].arg = arg;

    gpio_set_irq_enabled(gpio, event_mask, true);

    return true;
}

bool gpio_irq_unregister(uint gpio)
{
    if (gpio >= GPIO_IRQ_MAX_GPIOS) {
        return false;
    }

    gpio_set_irq_enabled(gpio, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL | GPIO_IRQ_LEVEL_HIGH | GPIO_IRQ_LEVEL_LOW, false);

    gpio_irq_table[gpio].event_mask = 0;
    gpio_irq_table[gpio].callback = NULL;
    gpio_irq_table[gpio].arg = NULL;
}
