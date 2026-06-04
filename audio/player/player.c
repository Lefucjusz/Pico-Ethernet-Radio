#include "player.h"
#include "audio_defs.h"
#include <audio_i2s.h>
#include <utils.h>
#include <ipc_message.h>
#include <ipc_context.h>
#include <logger.h>

#define PLAYER_TASK_NAME "player"
#define PLAYER_TASK_STACK_SIZE UTILS_STACK_BYTES_TO_WORDS(1024 * 1)
#define PLAYER_TASK_PRIO 2

#define PLAYER_DMA_BUFFER_SIZE_FRAMES (1024 * 1)
#define PLAYER_DMA_BUFFER_SIZE_BYTES AUDIO_FRAMES_TO_BYTES(PLAYER_DMA_BUFFER_SIZE_FRAMES)

#define PLAYER_DEFAULT_SAMPLE_RATE_HZ 44100

#define PLAYER_STATE_PROCESSING_PERIOD_TICKS pdMS_TO_TICKS(500)

typedef enum
{
    PLAYER_IDLE,
    PLAYER_BUFFERING,
    PLAYER_RUNNING
} player_state_t;

typedef struct
{
    const ipc_ctx_t *ipc;
    player_state_t state;
    audio_i2s_t i2s;
    audio_i2s_config_t i2s_cfg;
    size_t watermark_low;
    size_t watermark_high;
} player_ctx_t;

static player_ctx_t ctx;

static void player_dma_callback(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    audio_i2s_clear_dma_irq(&ctx.i2s);

    ipc_player_msg_t msg = {
        .type = IPC_MSG_PLAYER_DMA_REQUEST
    };
    xQueueSendFromISR(ctx.ipc->player_q, &msg, &xHigherPriorityTaskWoken);

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void player_scale_volume(int16_t *buffer, size_t frames_count)
{
    for (size_t i = 0; i < frames_count; ++i) {
        buffer[2 * i] /= 13;
        buffer[2 * i + 1] /= 13; // TODO
    }
}

static void player_feed_buffer(void)
{
    int16_t *buffer = audio_i2s_get_next_buffer(&ctx.i2s);
    const size_t bytes_read = xStreamBufferReceive(ctx.ipc->pcm_buffer, buffer, PLAYER_DMA_BUFFER_SIZE_BYTES, 0);
    if (bytes_read < PLAYER_DMA_BUFFER_SIZE_BYTES) {
        LOG_WARN("Buffer underrun!");
    }

    player_scale_volume(buffer, AUDIO_BYTES_TO_FRAMES(bytes_read));
}

static void player_report_running(void)
{
    ipc_manager_msg_t msg = {
        .type = IPC_MSG_PLAYER_RUNNING
    };
    xQueueSend(ctx.ipc->manager_q, &msg, 0);
}

static void player_report_stopped(void)
{
    ipc_manager_msg_t msg = {
        .type = IPC_MSG_PLAYER_STOPPED
    };
    xQueueSend(ctx.ipc->manager_q, &msg, 0);
}

static void player_report_buffering(void)
{
    ipc_manager_msg_t msg = {
        .type = IPC_MSG_PLAYER_BUFFERING
    };
    xQueueSend(ctx.ipc->manager_q, &msg, 0);
}

static void player_task(void *arg)
{
    ipc_player_msg_t msg;
    int err;

    int cnt = 0;
    size_t bytes_sum = 0;

    LOG_INFO("Started at core %d", portGET_CORE_ID());

    /* TODO defines */
    ctx.i2s_cfg.pio = pio0;
    ctx.i2s_cfg.clock_pin_base = 26;
    ctx.i2s_cfg.data_pin = 28;
    ctx.i2s_cfg.sample_size = AUDIO_SAMPLE_SIZE;
    ctx.i2s_cfg.sample_rate = PLAYER_DEFAULT_SAMPLE_RATE_HZ;
    ctx.i2s_cfg.buffer_frames_count = PLAYER_DMA_BUFFER_SIZE_FRAMES;
    ctx.i2s_cfg.dma_handler = player_dma_callback;

    ctx.i2s.config = &ctx.i2s_cfg;

    err = audio_i2s_init(&ctx.i2s);
    if (err) {
        LOG_FATAL("Failed to initialize I2S, error %d", err);
        configASSERT(0);
    }

    while (1) {
        /* State transitions */
        if (xQueueReceive(ctx.ipc->player_q, &msg, PLAYER_STATE_PROCESSING_PERIOD_TICKS) == pdTRUE) {
            switch (ctx.state) {
                case PLAYER_IDLE:
                    if (msg.type == IPC_MSG_PLAYER_START) {
                        audio_i2s_set_sample_rate(&ctx.i2s, msg.arg);
                        player_report_buffering();
                        ctx.state = PLAYER_BUFFERING;
                        LOG_DEBUG("IDLE -> BUFFERING");
                    }
                    break;

                case PLAYER_BUFFERING:
                    if (msg.type == IPC_MSG_PLAYER_STOP) {
                        player_report_stopped();
                        ctx.state = PLAYER_IDLE;
                        LOG_DEBUG("BUFFERING -> IDLE");
                    }
                    break;

                case PLAYER_RUNNING:
                    if (msg.type == IPC_MSG_PLAYER_STOP) {
                        audio_i2s_enable(&ctx.i2s, false);
                        player_report_stopped();
                        ctx.state = PLAYER_IDLE;
                        LOG_DEBUG("RUNNING -> IDLE");
                    }
                    else if (msg.type == IPC_MSG_PLAYER_DMA_REQUEST) {
                        player_feed_buffer();
                    }
                    break;

                default:
                    break;
            }
        }

        /* Periodic state processing, runs at any received event or every PLAYER_STATE_PROCESSING_PERIOD_TICKS */
        const size_t bytes_available = xStreamBufferBytesAvailable(ctx.ipc->pcm_buffer);

        {
            ++cnt;
            bytes_sum += bytes_available;
            if (cnt >= 100) {
                LOG_DEBUG("Mean PCM buffer level: %d%%", (100 * bytes_sum / cnt) / (ctx.watermark_low * 4));
                cnt = 0;
                bytes_sum = 0;
                vTaskDelay(1);
            }
        }

        switch (ctx.state) {
            case PLAYER_BUFFERING:
                if (bytes_available > ctx.watermark_high) {
                    LOG_DEBUG("Buffer ready: %uB", bytes_available);

                    audio_i2s_enable(&ctx.i2s, true);
                    player_report_running();

                    LOG_DEBUG("BUFFERING -> RUNNING");

                    ctx.state = PLAYER_RUNNING;
                }
                break;

            case PLAYER_RUNNING:
                if (bytes_available < ctx.watermark_low) {
                    LOG_DEBUG("Approaching underrun!");

                    audio_i2s_enable(&ctx.i2s, false);
                    player_report_buffering();

                    LOG_DEBUG("RUNNING -> BUFFERING");

                    ctx.state = PLAYER_BUFFERING;
                }
                break;

            default:
                break;
        }
    }
}

void player_init(void)
{
    ctx.ipc = ipc_context_get();

    const size_t pcm_buffer_size = xStreamBufferBytesAvailable(ctx.ipc->pcm_buffer) + xStreamBufferSpacesAvailable(ctx.ipc->pcm_buffer);
    ctx.watermark_low = 1 * pcm_buffer_size / 4; // 25%
    ctx.watermark_high = 3 * pcm_buffer_size / 4; // 75%

    xTaskCreate(player_task,
                PLAYER_TASK_NAME,
                PLAYER_TASK_STACK_SIZE,
                NULL,
                PLAYER_TASK_PRIO,
                NULL);
}
