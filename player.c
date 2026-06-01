#include <FreeRTOS.h>
#include <task.h>
#include <stream_buffer.h>
#include <helix_mp3.h>
#include <audio_i2s.h>
#include <stdio.h>

#define PLAYER_BUFFER_SIZE_FRAMES (1024 * 4)

extern StreamBufferHandle_t network_buffer;

static TaskHandle_t player_task;

static audio_i2s_t i2s;
static audio_i2s_config_t i2s_cfg;
static helix_mp3_t mp3;
static helix_mp3_io_t mp3_io;

static void player_dma_callback(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    audio_i2s_clear_dma_irq(&i2s);
    vTaskNotifyGiveFromISR(player_task, &xHigherPriorityTaskWoken);

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static size_t player_read_callback(void *user_data, void *buffer, size_t size)
{
    return xStreamBufferReceive(network_buffer, buffer, size, portMAX_DELAY);
}

static int player_seek_callback(void *user_data, int offset)
{
    return 0; // Dummy implementation
}

static void player_scale_volume(int16_t *buffer, size_t frames_count)
{
    for (size_t i = 0; i < frames_count; ++i) {
        buffer[2 * i] /= 16;
        buffer[2 * i + 1] /= 16; // TODO
    }
}

static void player_thread(void *arg)
{
    printf("%s: started at core %d\n", __func__, portGET_CORE_ID());

    vTaskDelay(2000);

    i2s_cfg.pio = pio0;
    i2s_cfg.clock_pin_base = 26;
    i2s_cfg.data_pin = 28;
    i2s_cfg.sample_size = 2;
    i2s_cfg.buffer_frames_count = PLAYER_BUFFER_SIZE_FRAMES;
    i2s_cfg.dma_handler = player_dma_callback;

    i2s.config = &i2s_cfg;

    mp3_io.read = player_read_callback;
    mp3_io.seek = player_seek_callback;

    int err;

    err = helix_mp3_init(&mp3, &mp3_io);
    if (err) {
        printf("Failed to initialize the decoder, error %d\n", err);
        configASSERT(0); // TODO this does not work?
    }

    i2s_cfg.sample_rate = helix_mp3_get_sample_rate(&mp3);
    printf("Detected sample rate: %uHz\n", i2s_cfg.sample_rate);

    err = audio_i2s_init(&i2s);
    if (err) {
        printf("Failed to initialize I2S, error %d\n", err);
        configASSERT(0);
    }

    // audio_i2s_enable(&i2s, true);

    int cnt = 0;
    bool enabled = false;

    while (1) {
        if (enabled) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
             
            int16_t *buffer = audio_i2s_get_next_buffer(&i2s);

            const size_t frames_read = helix_mp3_read_pcm_frames_s16(&mp3, buffer, PLAYER_BUFFER_SIZE_FRAMES);
            if (frames_read == 0) {
                printf("Helix returned no frames!\n");
                audio_i2s_enable(&i2s, false);
                configASSERT(0);
            }

            ++cnt;
            if (cnt > 100) {
                printf("%s: free stack: %u\n", __func__, uxTaskGetStackHighWaterMark(NULL) * 4);
                cnt = 0;
            }

            player_scale_volume(buffer, frames_read);

            const size_t buffered = xStreamBufferBytesAvailable(network_buffer);
            if (buffered <= 8192) {
                printf("%s: buffer low (%uB), pausing playback\n", __func__, buffered);
                audio_i2s_enable(&i2s, false);
                enabled = false;
            }
        }
        else {
            const size_t buffered = xStreamBufferBytesAvailable(network_buffer);
            if (buffered >= 24576) {
                    printf("%s: buffer refilled (%uB), starting playback\n", __func__, buffered);

                    /* Refill next PCM buffer */
                    // int16_t *buffer = audio_i2s_get_next_buffer(&i2s);
                    // const size_t frames_read = helix_mp3_read_pcm_frames_s16(&mp3, buffer, PLAYER_BUFFER_SIZE_FRAMES);
                    // player_scale_volume(buffer, frames_read);

                    audio_i2s_enable(&i2s, true);

                    // ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
                    enabled = true;
            }

            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

void player_start(void)
{
    xTaskCreateAffinitySet(player_thread, "player", 2048 / sizeof(uint32_t), NULL, 2, 1 << 0, &player_task); // TODO core1
}
