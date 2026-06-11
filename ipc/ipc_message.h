#pragma once

#include <radio_state.h>

typedef enum
{
    // Module -> manager messages
    IPC_MSG_NETWORK_LINK_DOWN,
    IPC_MSG_NETWORK_LINK_UP,
    IPC_MSG_NETWORK_GOT_IP,

    IPC_MSG_STREAM_RUNNING,
    IPC_MSG_STREAM_STOPPED,
    IPC_MSG_STREAM_FAIL,

    IPC_MSG_DECODER_RUNNING,
    IPC_MSG_DECODER_STOPPED,
    IPC_MSG_DECODER_BUFFERING,
    IPC_MSG_DECODER_FAIL,

    IPC_MSG_PLAYER_RUNNING,
    IPC_MSG_PLAYER_STOPPED,
    IPC_MSG_PLAYER_BUFFERING,
    IPC_MSG_PLAYER_DMA_REQUEST, // Internal message

    IPC_MSG_UI_START_PLAYBACK,
    IPC_MSG_UI_STOP_PLAYBACK,
    IPC_MSG_UI_SET_VOLUME,
    IPC_MSG_UI_GET_STATUS,



    // Manager -> module messages
    IPC_MSG_STREAM_START,
    IPC_MSG_STREAM_STOP,

    IPC_MSG_DECODER_START,
    IPC_MSG_DECODER_STOP,

    IPC_MSG_PLAYER_START,
    IPC_MSG_PLAYER_STOP,
    IPC_MSG_PLAYER_SET_VOLUME,

    IPC_MSG_UI_STATUS,

    IPC_MSG_MANAGER_RESTART_STREAM // Internal message

} ipc_msg_type_t;

typedef struct
{
    ipc_msg_type_t type;
    void *arg; // TODO maybe different type?
} ipc_manager_msg_t;

typedef struct
{
    ipc_msg_type_t type;
    const char *url;
} ipc_stream_msg_t;

typedef struct
{
    ipc_msg_type_t type;
} ipc_decoder_msg_t;

typedef struct
{
    ipc_msg_type_t type;
    uint16_t arg; // volume/sample rate
} ipc_player_msg_t;

typedef struct
{
    ipc_msg_type_t type;
    radio_status_t status;
} ipc_server_msg_t;
