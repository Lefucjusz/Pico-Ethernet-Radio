#pragma once

#include <stdint.h>

#define RADIO_STATE_STREAM_URL_MAX_SIZE 128

typedef enum
{
    RADIO_STATE_GET_LINK,
    RADIO_STATE_GET_IP,
    RADIO_STATE_READY,
    RADIO_STATE_STARTING_STREAM,
    RADIO_STATE_STARTING_DECODER,
    RADIO_STATE_STARTING_PLAYER,
    RADIO_STATE_PLAYBACK_RUNNING,
    RADIO_STATE_AWAITING_RESTART,
    RADIO_STATE_ERROR,
    RADIO_STATE_COUNT
} radio_state_t;

typedef struct
{
    radio_state_t state;
    char stream_url[RADIO_STATE_STREAM_URL_MAX_SIZE];
    uint8_t volume;
} radio_status_t;

const char *radio_state_get_name(radio_state_t state);
