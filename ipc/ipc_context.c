#include "ipc_context.h"
#include "ipc_message.h"

#define IPC_CONTEXT_MANAGER_QUEUE_LENGTH 4
#define IPC_CONTEXT_MODULE_QUEUE_LENGTH 2

#define IPC_CONTEXT_RECV_BUFFER_SIZE (1024 * 32)
#define IPC_CONTEXT_PCM_BUFFER_SIZE (1024 * 48)

static ipc_ctx_t ctx;

bool ipc_context_init(void)
{
    ctx.recv_buffer = xStreamBufferCreate(IPC_CONTEXT_RECV_BUFFER_SIZE, 0);
    ctx.pcm_buffer = xStreamBufferCreate(IPC_CONTEXT_PCM_BUFFER_SIZE, 0);

    ctx.manager_q = xQueueCreate(IPC_CONTEXT_MANAGER_QUEUE_LENGTH, sizeof(ipc_manager_msg_t)); // TODO error handling
    ctx.stream_q = xQueueCreate(IPC_CONTEXT_MODULE_QUEUE_LENGTH, sizeof(ipc_stream_msg_t));
    ctx.decoder_q = xQueueCreate(IPC_CONTEXT_MODULE_QUEUE_LENGTH, sizeof(ipc_decoder_msg_t));
    ctx.player_q = xQueueCreate(IPC_CONTEXT_MODULE_QUEUE_LENGTH, sizeof(ipc_player_msg_t));
    ctx.server_q = xQueueCreate(IPC_CONTEXT_MODULE_QUEUE_LENGTH, sizeof(ipc_server_msg_t));

    return true;
}

const ipc_ctx_t *ipc_context_get(void)
{
    return &ctx;
}
