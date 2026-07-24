#include "helix_mp3.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define HELIX_MP3_MIN(x, y) (((x) < (y)) ? (x) : (y))
#define HELIX_MP3_SAMPLES_PER_FRAME 2

static size_t helix_mp3_fill_mp3_buffer(helix_mp3_t *mp3)
{
    /* Move remaining data to the beginning of the buffer */
    memmove(&mp3->mp3_buffer[0], mp3->mp3_read_ptr, mp3->mp3_buffer_bytes_left);

    /* Read new data */
    const size_t bytes_to_read = HELIX_MP3_DATA_CHUNK_SIZE - mp3->mp3_buffer_bytes_left;
    const size_t bytes_read = mp3->io->read(mp3->io->user_data, &mp3->mp3_buffer[mp3->mp3_buffer_bytes_left], sizeof(*mp3->mp3_buffer) * bytes_to_read);

    /* Zero-pad to avoid finding false sync word from old data */
    if (bytes_read < bytes_to_read) {
        memset(&mp3->mp3_buffer[mp3->mp3_buffer_bytes_left + bytes_read], 0, bytes_to_read - bytes_read);
    }

    return bytes_read;
}

static void helix_mp3_convert_pcm_mono_to_stereo(helix_mp3_t *mp3)
{
    for (int32_t i = mp3->pcm_samples_left - 1; i >= 0; --i) {
        mp3->pcm_buffer[2 * i] = mp3->pcm_buffer[i];
        mp3->pcm_buffer[2 * i + 1] = mp3->pcm_buffer[i];
    }
    mp3->pcm_samples_left *= 2;
}

static size_t helix_mp3_decode_next_frame(helix_mp3_t *mp3)
{
    size_t pcm_samples_read;

    while (1) {
        if (mp3->mp3_buffer_bytes_left < HELIX_MP3_MIN_DATA_CHUNK_SIZE) {
            const size_t bytes_read = helix_mp3_fill_mp3_buffer(mp3);
            mp3->mp3_buffer_bytes_left += bytes_read;
            mp3->mp3_read_ptr = &mp3->mp3_buffer[0];
        }

        const int offset = MP3FindSyncWord(mp3->mp3_read_ptr, mp3->mp3_buffer_bytes_left);
        if (offset < 0) {
            pcm_samples_read = 0;
            break; // Out of data
        }
        mp3->mp3_read_ptr += offset;
        mp3->mp3_buffer_bytes_left -= offset;

        const int err = MP3Decode(mp3->dec, &mp3->mp3_read_ptr, &mp3->mp3_buffer_bytes_left, mp3->pcm_buffer, 0);
        if (err == ERR_MP3_NONE) {
            MP3FrameInfo frame_info;
            MP3GetLastFrameInfo(mp3->dec, &frame_info);

            mp3->current_sample_rate = frame_info.samprate;
            mp3->current_bitrate = frame_info.bitrate;
            mp3->pcm_samples_left = frame_info.outputSamps;
            if (frame_info.nChans == 1) {
                helix_mp3_convert_pcm_mono_to_stereo(mp3); // Output data always in 2-channel format
            }

            pcm_samples_read = mp3->pcm_samples_left;
            break;
        }
        else if (err == ERR_MP3_MAINDATA_UNDERFLOW) {
            continue; // Get more data from file
        }
        else if (mp3->mp3_buffer_bytes_left > 0) {
            mp3->mp3_read_ptr++;
            mp3->mp3_buffer_bytes_left--;
            continue; // False sync word, skip first byte and try again
        }
        else {
            pcm_samples_read = 0;
            break; // Out of data
        }
    }

    return pcm_samples_read;
}

int helix_mp3_init(helix_mp3_t *mp3, const helix_mp3_io_t *io)
{
    /* Sanity check */
    if ((mp3 == NULL) || (io == NULL)) {
        return -EINVAL;
    }

    /* Initialize decoder context */
    memset(mp3, 0, sizeof(*mp3));
    mp3->io = io;

    int err = 0;
    do {
        /* Initialize decoder */
        mp3->dec = MP3InitDecoder();
        if (mp3->dec == NULL) {
            err = -ENOMEM;
            break;
        }
        
        /* Initialize buffers */
        mp3->mp3_buffer = malloc(HELIX_MP3_DATA_CHUNK_SIZE);
        if (mp3->mp3_buffer == NULL) {
            err = -ENOMEM;
            break;
        }
        mp3->pcm_buffer = malloc(HELIX_MP3_MAX_SAMPLES_PER_FRAME * sizeof(*mp3->pcm_buffer));
        if (mp3->pcm_buffer == NULL) {
            err = -ENOMEM;
            break;
        }
    } while (0);

    if (err) {
        free(mp3->pcm_buffer);
        free(mp3->mp3_buffer);
        MP3FreeDecoder(mp3->dec);
    }
    return err;
}

int helix_mp3_deinit(helix_mp3_t *mp3)
{
    if (mp3 == NULL) {
        return -EINVAL;
    }

    if (mp3->pcm_buffer != NULL) {
        free(mp3->pcm_buffer);
        mp3->pcm_buffer = NULL;
    }
    if (mp3->mp3_buffer != NULL) {
        free(mp3->mp3_buffer);
        mp3->mp3_buffer = NULL;
    }
    MP3FreeDecoder(mp3->dec);

    return 0;
}

int helix_mp3_reset(helix_mp3_t *mp3)
{
    if (mp3 == NULL) {
        return -EINVAL;
    }

    mp3->pcm_samples_left = 0;
    mp3->mp3_buffer_bytes_left = 0;
    mp3->mp3_read_ptr = &mp3->mp3_buffer[0];

    return 0;
}

uint32_t helix_mp3_get_sample_rate(helix_mp3_t *mp3)
{
    if (mp3 == NULL) {
        return 0;
    }
    return mp3->current_sample_rate;
}

uint32_t helix_mp3_get_bitrate(helix_mp3_t *mp3)
{
    if (mp3 == NULL) {
        return 0;
    }
    return mp3->current_bitrate;
}

size_t helix_mp3_get_pcm_frames_decoded(helix_mp3_t *mp3)
{
    if (mp3 == NULL) {
        return 0;
    }
	return mp3->current_pcm_frame;
}

size_t helix_mp3_read_pcm_frames_s16(helix_mp3_t *mp3, int16_t *buffer, size_t frames_to_read)
{
    if ((mp3 == NULL) || (buffer == NULL) || (frames_to_read == 0)) {
        return 0;
    }

    size_t samples_to_read = frames_to_read * HELIX_MP3_SAMPLES_PER_FRAME;
    size_t samples_read = 0;

    while (1) {
        const size_t samples_to_consume = HELIX_MP3_MIN(mp3->pcm_samples_left, samples_to_read);
        
        /* Get samples from in-memory PCM buffer */
        memcpy(&buffer[samples_read], &mp3->pcm_buffer[HELIX_MP3_MAX_SAMPLES_PER_FRAME - mp3->pcm_samples_left], samples_to_consume * sizeof(*mp3->pcm_buffer));

        mp3->current_pcm_frame += (samples_to_consume / HELIX_MP3_SAMPLES_PER_FRAME);
        mp3->pcm_samples_left -= samples_to_consume;
        samples_read += samples_to_consume;
        samples_to_read -= samples_to_consume;

        /* In-memory PCM buffer is fully used up, decode next frame */
        if (mp3->pcm_samples_left == 0) {
            if (helix_mp3_decode_next_frame(mp3) == 0) {
                break;
            }
        }

        /* Job done */
        if (samples_to_read == 0) {
            break;
        } 
    }

    return (samples_read / HELIX_MP3_SAMPLES_PER_FRAME);
}
