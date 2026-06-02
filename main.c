#include <pico/stdlib.h>
#include <FreeRTOS.h>
#include <task.h>
#include <logger.h>
#include <ipc_context.h>
#include <network.h>
#include <connection.h>
#include <decoder.h>
#include <player.h>
#include <event_manager.h>
#include <hardware/clocks.h>
#include <pico/multicore.h>

#define LED_PIN 25

static void bootstrap_task(void *arg)
{
    logger_init();

    LOG_INFO("Started at core %d", portGET_CORE_ID());

    ipc_context_init();
    network_init();
    connection_init();
    decoder_init();
    player_init();

    event_manager_init();

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    /* Heartbeat */
    while (1) {
        gpio_xor_mask(1 << LED_PIN);
        vTaskDelay(1000);
    }
}

int main(void)
{
    set_sys_clock_khz(133000, true);

    stdio_init_all();

    multicore_reset_core1();

    // xTaskCreateAffinitySet(bootstrap_task, "bootstrap", 2048 / sizeof(uint32_t), NULL, 1, 1 << 0, NULL); // TODO defines

    xTaskCreate(bootstrap_task, "bootstrap", 2048 / sizeof(uint32_t), NULL, 1, NULL);

    vTaskStartScheduler();

    /* Unreachable */
    while (1) {}
}
