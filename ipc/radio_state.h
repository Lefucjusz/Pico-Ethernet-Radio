#pragma once

#include <stdint.h>

typedef enum
{
    RADIO_STATE_GET_LINK,
    RADIO_STATE_GET_IP,
    RADIO_STATE_READY,
    RADIO_STATE_CONNECTING,
    RADIO_STATE_STARTING_DECODER,
    RADIO_STATE_STARTING_PLAYER,
    RADIO_STATE_PLAYBACK_RUNNING,
    RADIO_STATE_COUNT
} radio_state_t;

typedef struct
{
    char host[64];
    char path[64];
    uint16_t port;
} radio_url_t;

typedef struct
{
    radio_state_t state;
    radio_url_t stream_url;
    uint8_t volume;
} radio_status_t;

const char *radio_state_get_name(radio_state_t state);
