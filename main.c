#include <pico/stdlib.h>
#include <hardware/clocks.h>
#include <FreeRTOS.h>
#include <task.h>
#include <ipc_context.h>
#include <network.h>
#include <connection.h>
#include <decoder.h>
#include <player.h>
#include <event_manager.h>
#include <gpio_irq.h>
#include <utils.h>
#include <logger.h>

#define BOOTSTRAP_TASK_NAME "bootstrap"
#define BOOTSTRAP_TASK_STACK_SIZE UTILS_STACK_BYTES_TO_WORDS(1024 * 1)
#define BOOTSTRAP_TASK_PRIO 1

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

    /* Heartbeat */
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    while (1) {
        gpio_xor_mask(1 << LED_PIN);
        vTaskDelay(1000);
    }
}

int main(void)
{
    timer_hw->dbgpause = 0;

    set_sys_clock_khz(150000, true);

    stdio_init_all();
    gpio_irq_init();

    xTaskCreate(bootstrap_task,
                BOOTSTRAP_TASK_NAME,
                BOOTSTRAP_TASK_STACK_SIZE,
                NULL,
                BOOTSTRAP_TASK_PRIO,
                NULL);

    vTaskStartScheduler();

    /* Unreachable */
    while (1) {}
}
