#include "player.h"
#include "audio_defs.h"
#include <audio_i2s.h>
#include <utils.h>
#include <ipc_message.h>
#include <ipc_context.h>
#include <logger.h>

#define PLAYER_TASK_NAME "player_task"
#define PLAYER_TASK_STACK_SIZE UTILS_STACK_BYTES_TO_WORDS(1024 * 2)
#define PLAYER_TASK_PRIO 1
#define PLAYER_TASK_CORE_AFFINITY 0

#define PLAYER_DMA_BUFFER_SIZE_FRAMES (1024 * 1)
#define PLAYER_DMA_BUFFER_SIZE_BYTES AUDIO_FRAMES_TO_BYTES(PLAYER_DMA_BUFFER_SIZE_FRAMES)

#define PLAYER_BUFFER_HEALTH_CHECK_PERIOD_TICKS pdMS_TO_TICKS(500)

typedef struct
{
    const ipc_ctx_t *ipc;
    audio_i2s_t i2s;
    audio_i2s_config_t i2s_cfg;
    size_t watermark_low;
    size_t watermark_high;
    bool underrun;
} player_ctx_t;

static player_ctx_t ctx;

static void player_dma_callback(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    audio_i2s_clear_dma_irq(&ctx.i2s);

    ipc_player_msg_t msg = {.type = IPC_MSG_PLAYER_DMA_REQUEST };
    xQueueSendFromISR(ctx.ipc->player_q, &msg, &xHigherPriorityTaskWoken);

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void player_scale_volume(int16_t *buffer, size_t frames_count)
{
    for (size_t i = 0; i < frames_count; ++i) {
        buffer[2 * i] /= 16;
        buffer[2 * i + 1] /= 16; // TODO
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

static void player_task(void *arg)
{
    int err;

    ipc_manager_msg_t manager_msg;
    ipc_player_msg_t player_msg;

    int cnt = 0;
    size_t bytes_sum = 0;

    LOG_INFO("Started at core %d", portGET_CORE_ID());

    ctx.i2s_cfg.pio = pio0;
    ctx.i2s_cfg.clock_pin_base = 26;
    ctx.i2s_cfg.data_pin = 28;
    ctx.i2s_cfg.sample_size = AUDIO_SAMPLE_SIZE;
    ctx.i2s_cfg.buffer_frames_count = PLAYER_DMA_BUFFER_SIZE_FRAMES;
    ctx.i2s_cfg.dma_handler = player_dma_callback;

    ctx.i2s.config = &ctx.i2s_cfg;

    // err = audio_i2s_init(&ctx.i2s);
    // if (err) {
    //     LOG_FATAL("Failed to initialize I2S, error %d", err);
    //     configASSERT(0);
    // }

    while (1) {
        /* Get next event */
        if (xQueueReceive(ctx.ipc->player_q, &player_msg, PLAYER_BUFFER_HEALTH_CHECK_PERIOD_TICKS) == pdTRUE) {
            switch (player_msg.type) {
                case IPC_MSG_PLAYER_START:
                    ctx.i2s_cfg.sample_rate = player_msg.arg;
                    audio_i2s_init(&ctx.i2s);
                    audio_i2s_enable(&ctx.i2s, true);
                    // ctx.playing = true;

                    break;

                case IPC_MSG_PLAYER_STOP:
                    audio_i2s_enable(&ctx.i2s, false);
                    // ctx.playing = false;
                    break;

                // case IPC_MSG_PLAYER_SET_SAMPLE_RATE:
                //     ctx.i2s_cfg.sample_rate = msg.arg; // TODO not available when playing
                //     // TODO audio_i2s layer
                //     break;

                // case IPC_MSG_PLAYER_SET_VOLUME:
                //     // TODO
                //     break;

                case IPC_MSG_PLAYER_DMA_REQUEST:
                    player_feed_buffer();
                    break;

                default:
                    LOG_ERROR("Unhandled event: %d", player_msg.type);
                    break;
            }
        }

        /* Check buffer health */
        const size_t bytes_available = xStreamBufferBytesAvailable(ctx.ipc->pcm_buffer);

        ++cnt;
        bytes_sum += bytes_available;
        if (cnt >= 100) {
            LOG_DEBUG("Mean PCM buffer level: %d%%", (100 * bytes_sum / cnt) / (ctx.watermark_low * 4));
            cnt = 0;
            bytes_sum = 0;
        }

        if (!ctx.underrun && (bytes_available < ctx.watermark_low)) {
            ctx.underrun = true;

            manager_msg.type = IPC_MSG_PLAYER_UNDERRUN;
            xQueueSend(ctx.ipc->manager_q, &manager_msg, 0);
        }
        else if (ctx.underrun && (bytes_available > ctx.watermark_high)) {
            ctx.underrun = false;

            // manager_msg.type = IPC_MSG_PLAYER_UNDERRUN;
            // xQueueSend(ctx.ipc->manager_q, &manager_msg, 0); // TODO
        }
    }
}

void player_init(void)
{
    ctx.ipc = ipc_context_get();

    const size_t pcm_buffer_size = xStreamBufferBytesAvailable(ctx.ipc->pcm_buffer) + xStreamBufferSpacesAvailable(ctx.ipc->pcm_buffer);
    ctx.watermark_low = 1 * pcm_buffer_size / 4; // 25%
    ctx.watermark_high = 3 * pcm_buffer_size / 4; // 75%

    // xTaskCreateAffinitySet(player_task, 
    //                        PLAYER_TASK_NAME, 
    //                        PLAYER_TASK_STACK_SIZE, 
    //                        NULL, 
    //                        PLAYER_TASK_PRIO, 
    //                        1 << PLAYER_TASK_CORE_AFFINITY, 
    //                        NULL);

    xTaskCreate(player_task, 
                PLAYER_TASK_NAME, 
                PLAYER_TASK_STACK_SIZE, 
                NULL, 
                PLAYER_TASK_PRIO,
                NULL);
}
