#pragma once

#include <FreeRTOS.h>
#include <stream_buffer.h>
#include <queue.h>

typedef struct
{
    StreamBufferHandle_t recv_buffer;
    StreamBufferHandle_t pcm_buffer;
    QueueHandle_t manager_q;
    QueueHandle_t lwip_q;
    QueueHandle_t conn_q;
    QueueHandle_t decoder_q;
    QueueHandle_t player_q;
    QueueHandle_t server_q;
} ipc_ctx_t;

bool ipc_context_init(void);

const ipc_ctx_t *ipc_context_get(void);
