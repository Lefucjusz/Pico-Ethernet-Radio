#include "decoder.h"
#include "audio_defs.h"
#include <helix_mp3.h>
#include <ipc_context.h>
#include <ipc_message.h>
#include <utils.h>
#include <logger.h>

#define DECODER_TASK_NAME "decoder"
#define DECODER_TASK_STACK_SIZE UTILS_STACK_BYTES_TO_WORDS(1024 * 2)
#define DECODER_TASK_PRIO 1

#define DECODER_BUFFER_SIZE_FRAMES 1024
#define DECODER_BUFFER_SIZE_BYTES AUDIO_FRAMES_TO_BYTES(DECODER_BUFFER_SIZE_FRAMES)

#define DECODER_STATE_PROCESSING_PERIOD_TICKS pdMS_TO_TICKS(500)

typedef enum
{
    DECODER_IDLE,
    DECODER_BUFFERING,
    DECODER_RUNNING
} decoder_state_t;

typedef struct
{
    const ipc_ctx_t *ipc;
    decoder_state_t state;
    helix_mp3_t mp3;
    helix_mp3_io_t mp3_io;
    uint8_t pcm_buffer[DECODER_BUFFER_SIZE_BYTES];
    size_t watermark_low;
    size_t watermark_high;
} decoder_ctx_t;

static decoder_ctx_t ctx;

static size_t decoder_read_callback(void *user_data, void *buffer, size_t size)
{
    size_t bytes = xStreamBufferReceive(ctx.ipc->recv_buffer, buffer, size, portMAX_DELAY);
    if (bytes < size) {
        LOG_ERROR("Approaching underrun!");
    }
    return bytes;
}

static int decoder_seek_callback(void *user_data, int offset)
{
    return 0; // Dummy implementation
}

static void decoder_report_running(void)
{
    ipc_manager_msg_t msg = {
        .type = IPC_MSG_DECODER_RUNNING,
        .arg = (void *)helix_mp3_get_sample_rate(&ctx.mp3)
    };
    xQueueSend(ctx.ipc->manager_q, &msg, 0);
}

static void decoder_report_stopped(void)
{
    ipc_manager_msg_t msg = {
        .type = IPC_MSG_DECODER_STOPPED
    };
    xQueueSend(ctx.ipc->manager_q, &msg, 0);
}

static void decoder_report_buffering(void)
{
    ipc_manager_msg_t msg = {
        .type = IPC_MSG_DECODER_BUFFERING
    };
    xQueueSend(ctx.ipc->manager_q, &msg, 0);
}

static void decoder_task(void *arg)
{
    ctx.mp3_io.read = decoder_read_callback;
    ctx.mp3_io.seek = decoder_seek_callback;

    ipc_decoder_msg_t msg;

    int cnt = 0;
    size_t bytes_sum = 0;

    LOG_INFO("Started at core %d", portGET_CORE_ID());

    while (1) {
        /* Check if any message pending */
        if (xQueueReceive(ctx.ipc->decoder_q, &msg, ctx.state == DECODER_IDLE ? portMAX_DELAY : 1) == pdTRUE) {
            switch (ctx.state) {
                case DECODER_IDLE:
                    if (msg.type == IPC_MSG_DECODER_START) {
                        decoder_report_buffering();
                        ctx.state = DECODER_BUFFERING;
                        LOG_DEBUG("IDLE -> BUFFERING");
                    }
                    break;

                case DECODER_BUFFERING:
                    if (msg.type == IPC_MSG_DECODER_STOP) {
                        decoder_report_stopped();
                        ctx.state = DECODER_IDLE;
                        LOG_DEBUG("BUFFERING -> IDLE");
                    }
                    break;

                case DECODER_RUNNING:
                    if (msg.type == IPC_MSG_DECODER_STOP) {
                        decoder_report_stopped();
                        ctx.state = DECODER_IDLE;
                        LOG_DEBUG("RUNNING -> IDLE");
                    }
                    break;

                default:
                    break;
            }
        }

        /* Periodic state processing, runs at any received event or every ??? */
        const size_t bytes_available = xStreamBufferBytesAvailable(ctx.ipc->recv_buffer);

        switch (ctx.state) {
            case DECODER_BUFFERING:
                vTaskDelay(100);
                if (bytes_available > ctx.watermark_high) {
                    helix_mp3_deinit(&ctx.mp3); // TODO this is very costly recovery strategy
                    int err = helix_mp3_init(&ctx.mp3, &ctx.mp3_io);
                    if (err) {
                        LOG_ERROR("Init failed, error %d", err);
                    }
                    else {
                        decoder_report_running();
                        ctx.state = DECODER_RUNNING;
                    }
                }
                break;

            case DECODER_RUNNING:
                if (bytes_available < ctx.watermark_low) {
                    decoder_report_buffering();
                    ctx.state = DECODER_BUFFERING;
                }
                else {
                    const size_t frames_read = helix_mp3_read_pcm_frames_s16(&ctx.mp3, (int16_t *)ctx.pcm_buffer, DECODER_BUFFER_SIZE_FRAMES);
                    xStreamBufferSend(ctx.ipc->pcm_buffer, ctx.pcm_buffer, AUDIO_FRAMES_TO_BYTES(frames_read), 100); // TODO magic number
                }
                break;

            default:
                break;
        }



        ++cnt;
        bytes_sum += bytes_available;
        if (cnt >= 100) {
            LOG_DEBUG("Mean recv buffer level: %d%%", (100 * bytes_sum / cnt) / (ctx.watermark_low * 4));
            cnt = 0;
            bytes_sum = 0;
        }
    }
}

void decoder_init(void)
{
    ctx.ipc = ipc_context_get();

    const size_t recv_buffer_size = xStreamBufferBytesAvailable(ctx.ipc->recv_buffer) + xStreamBufferSpacesAvailable(ctx.ipc->recv_buffer);
    ctx.watermark_low = 1 * recv_buffer_size / 4;   // 25%
    ctx.watermark_high = 3 * recv_buffer_size / 4;  // 75%

    xTaskCreate(decoder_task,
                DECODER_TASK_NAME,
                DECODER_TASK_STACK_SIZE,
                NULL,
                DECODER_TASK_PRIO,
                NULL);
}
