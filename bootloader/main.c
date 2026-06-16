#include <pico/stdlib.h>
#include <update.h>
#include <image.h>
#include <stdio.h>

static void halt_and_catch_fire(void)
{
    printf("Execution halted");

    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

    while (1) {
        gpio_xor_mask(1 << PICO_DEFAULT_LED_PIN);
        sleep_ms(250);
    }
}

static void process_update(void)
{
    if (!update_check_pending()) {
        printf("No pending update\n");
        return;
    }

    printf("Update pending, applying...\n");
    if (!update_apply()) {
        printf("Update failed!\n");
    }
    else {
        printf("Update successful!\n");
    }
}

int main(void)
{
    timer_hw->dbgpause = 0;

    stdio_init_all();

    printf("Pico Radio Bootloader, build " __DATE__ " " __TIME__ "\n");

    /* Try to apply update */
    process_update();

    /* Validate active image */
    printf("Validating app image...\n");
    if (!image_validate_active()) {
        halt_and_catch_fire();
    }

    /* Boot the app */
    printf("Booting app...\n");
    image_boot();
}
