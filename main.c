#include <pico/stdlib.h>
#include <hardware/clocks.h>
#include <FreeRTOS.h>
#include <task.h>
#include <stream_buffer.h>
#include <ipc_context.h>
#include <network.h>
#include <connection.h>
#include <server.h>
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

static void update_stream_bufs_state(void)
{
    static size_t cnt;
    static size_t recv_sum;
    static size_t pcm_sum;

    const ipc_ctx_t *ipc = ipc_context_get();

    const size_t recv_total = xStreamBufferBytesAvailable(ipc->recv_buffer) + xStreamBufferSpacesAvailable(ipc->recv_buffer);
    const size_t pcm_total = xStreamBufferBytesAvailable(ipc->pcm_buffer) + xStreamBufferSpacesAvailable(ipc->pcm_buffer);

    recv_sum += xStreamBufferBytesAvailable(ipc->recv_buffer);
    pcm_sum += xStreamBufferBytesAvailable(ipc->pcm_buffer);

    ++cnt;
    if (cnt >= 100) {
        LOG_DEBUG("Mean TCP buffer level: %u%%", (100 * recv_sum / cnt) / recv_total);
        LOG_DEBUG("Mean PCM buffer level: %u%%", (100 * pcm_sum / cnt) / pcm_total);
        cnt = 0;
        recv_sum = 0;
        pcm_sum = 0;
    }
}

static void update_led_state(void)
{
    static size_t cnt;

    ++cnt;
    if (cnt >= 10) {
        gpio_xor_mask(1 << LED_PIN);
        cnt = 0;
    }
}

static void bootstrap_task(void *arg)
{
    logger_init();

    LOG_INFO("Started at core %d", portGET_CORE_ID());

    ipc_context_init();
    network_init();
    connection_init();
    server_init();
    decoder_init();
    player_init();
    event_manager_init();

    /* Heartbeat */
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    while (1) {
        update_stream_bufs_state();
        update_led_state();
        vTaskDelay(100);
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
