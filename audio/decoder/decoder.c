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
#define DECODER_TASK_CORE_AFFINITY 0

#define DECODER_BUFFER_SIZE_FRAMES 1024
#define DECODER_BUFFER_SIZE_BYTES AUDIO_FRAMES_TO_BYTES(DECODER_BUFFER_SIZE_FRAMES)

typedef enum
{
    DECODER_IDLE,
    DECODER_PREPARE,
    DECODER_READY,
    DECODER_RUNNING,
    DECODER_UNDERRUN
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
    return xStreamBufferReceive(ctx.ipc->recv_buffer, buffer, size, portMAX_DELAY);
}

static int decoder_seek_callback(void *user_data, int offset)
{
    return 0; // Dummy implementation
}

static void decoder_prepare(void)
{
    int err;
    for (int i = 0; i < 3; ++i) {
        err = helix_mp3_init(&ctx.mp3, &ctx.mp3_io);
        if (!err) {
            LOG_DEBUG("attempt %d", i);
            break;
        }
    }
    // int err = helix_mp3_init(&ctx.mp3, &ctx.mp3_io);
    if (err) {
        LOG_ERROR("Failed to initialize decoder, error %d", err);

        ipc_manager_msg_t manager_msg;
        manager_msg.type = IPC_MSG_DECODER_FAIL;
        xQueueSend(ctx.ipc->manager_q, &manager_msg, 0);

        ctx.state = DECODER_IDLE;
    }
    else {
        ctx.state = DECODER_READY;    
    }
}

static TickType_t decoder_get_block_time(void)
{
    if ((ctx.state == DECODER_IDLE) || (ctx.state == DECODER_READY)) {
        return portMAX_DELAY;
    }
    return 0;
}

static void decoder_task(void *arg)
{
    ctx.mp3_io.read = decoder_read_callback;
    ctx.mp3_io.seek = decoder_seek_callback;

    ipc_manager_msg_t manager_msg;
    ipc_decoder_msg_t decoder_msg;

    int cnt = 0;
    size_t bytes_sum = 0;

    while (1) {
        /* Check if any message pending */
        if (xQueueReceive(ctx.ipc->decoder_q, &decoder_msg, decoder_get_block_time()) == pdTRUE) {
            switch (ctx.state) {
                case DECODER_IDLE:
                    if (decoder_msg.type == IPC_MSG_DECODER_INIT) {
                       ctx.state = DECODER_PREPARE;
                    }
                    break;
                
                case DECODER_READY:
                    if (decoder_msg.type == IPC_MSG_DECODER_INIT) {
                        ctx.state = DECODER_PREPARE;
                    }
                    else if (decoder_msg.type == IPC_MSG_DECODER_START) {
                        manager_msg.type = IPC_MSG_DECODER_RUNNING;
                        xQueueSend(ctx.ipc->manager_q, &manager_msg, 0);

                        ctx.state = DECODER_RUNNING;
                    }
                    break;
                
                case DECODER_RUNNING:
                case DECODER_UNDERRUN:
                    if (decoder_msg.type == IPC_MSG_DECODER_STOP) {
                        manager_msg.type = IPC_MSG_DECODER_STOPPED;
                        xQueueSend(ctx.ipc->manager_q, &manager_msg, 0);

                        ctx.state = DECODER_READY;
                    }
                    break;

                default:
                    LOG_ERROR("Got message in invalid state: %d", ctx.state);
                    break;
            }
        }

        /* Decode next frame */
        if ((ctx.state == DECODER_RUNNING) || (ctx.state == DECODER_UNDERRUN)) {
            const size_t frames_read = helix_mp3_read_pcm_frames_s16(&ctx.mp3, (int16_t *)ctx.pcm_buffer, DECODER_BUFFER_SIZE_FRAMES);
            xStreamBufferSend(ctx.ipc->pcm_buffer, ctx.pcm_buffer, AUDIO_FRAMES_TO_BYTES(frames_read), portMAX_DELAY); // TODO this will lock
        }

        /* Check buffer health */
        const size_t bytes_available = xStreamBufferBytesAvailable(ctx.ipc->recv_buffer);

        ++cnt;
        bytes_sum += bytes_available;
        if (cnt >= 100) {
            LOG_DEBUG("Mean recv buffer level: %d%%", (100 * bytes_sum / cnt) / (ctx.watermark_low * 4));
            cnt = 0;
            bytes_sum = 0;
        }

        // vTaskDelay(1);

        if ((ctx.state == DECODER_RUNNING) && (bytes_available < ctx.watermark_low)) {
            ctx.state = DECODER_UNDERRUN;

            manager_msg.type = IPC_MSG_DECODER_UNDERRUN;
            xQueueSend(ctx.ipc->manager_q, &manager_msg, 0);
        }
        else if (bytes_available > ctx.watermark_high) {
            if (ctx.state == DECODER_UNDERRUN) {
                ctx.state = DECODER_RUNNING;

                manager_msg.type = IPC_MSG_DECODER_READY;
                xQueueSend(ctx.ipc->manager_q, &manager_msg, 0);
            }
            else if (ctx.state == DECODER_PREPARE) {
                LOG_DEBUG("recv buffer filled, level %dB", bytes_available);
                decoder_prepare(); // TODO this seems to be overly spaghetti

                manager_msg.type = IPC_MSG_DECODER_READY;
                manager_msg.arg = (void *)helix_mp3_get_sample_rate(&ctx.mp3);
                xQueueSend(ctx.ipc->manager_q, &manager_msg, 0);
            }
        }
    }
}

void decoder_init(void)
{
    ctx.ipc = ipc_context_get();

    const size_t recv_buffer_size = xStreamBufferBytesAvailable(ctx.ipc->recv_buffer) + xStreamBufferSpacesAvailable(ctx.ipc->recv_buffer);
    ctx.watermark_low = 1 * recv_buffer_size / 4;   // 25%
    ctx.watermark_high = 3 * recv_buffer_size / 4;  // 75%

    // xTaskCreateAffinitySet(decoder_task, 
    //                        DECODER_TASK_NAME,
    //                        DECODER_TASK_STACK_SIZE,
    //                        NULL,
    //                        DECODER_TASK_PRIO,
    //                        1 << DECODER_TASK_CORE_AFFINITY,
    //                        NULL);

    xTaskCreate(decoder_task, 
                           DECODER_TASK_NAME,
                           DECODER_TASK_STACK_SIZE,
                           NULL,
                           DECODER_TASK_PRIO,
                           NULL);
}
