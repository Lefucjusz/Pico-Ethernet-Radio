#include "event_manager.h"
#include <FreeRTOS.h>
#include <task.h>
#include <utils.h>
#include <ipc_context.h>
#include <ipc_message.h>
#include <logger.h>

#define EVT_MGR_TASK_NAME "event_manager"
#define EVT_MGR_TASK_STACK_SIZE UTILS_STACK_BYTES_TO_WORDS(1024 * 1)
#define EVT_MGR_TASK_PRIO 1

typedef enum
{
    EVT_MGR_STATE_GET_LINK,
    EVT_MGR_STATE_GET_IP,
    EVT_MGR_STATE_CONNECTING,
    EVT_MGR_STATE_STARTING_DECODER,
    EVT_MGR_STATE_STARTING_PLAYER,
    EVT_MGR_STATE_PLAYBACK_RUNNING
} evt_mgr_state_t;

static const char *evt_mgr_state_str[] = {
    "GET_LINK",
    "GET_IP",
    "CONNECTING",
    "STARTING_DECODER",
    "STARTING_PLAYER",
    "PLAYBACK_RUNNING"
};

static evt_mgr_state_t state;

static void event_manager_task(void *arg)
{
    const ipc_ctx_t *ipc = ipc_context_get();

    ipc_manager_msg_t msg;
    ipc_connection_msg_t conn_msg;
    ipc_decoder_msg_t dec_msg;
    ipc_player_msg_t player_msg;

    evt_mgr_state_t next_state;

    LOG_INFO("Started at core %d", portGET_CORE_ID());

    while (1) {
        xQueueReceive(ipc->manager_q, &msg, portMAX_DELAY);

        switch (state) {
            case EVT_MGR_STATE_GET_LINK:
                if (msg.type == IPC_MSG_NETWORK_LINK_UP) {
                    LOG_INFO("Link UP!");
                    next_state = EVT_MGR_STATE_GET_IP;
                }
                break;

            case EVT_MGR_STATE_GET_IP:
                if (msg.type == IPC_MSG_NETWORK_GOT_IP) {
                    conn_msg.type = IPC_MSG_CONNECTION_START;
                    conn_msg.port = 8904;
                    conn_msg.host = "mp3.polskieradio.pl";
                    xQueueSend(ipc->conn_q, &conn_msg, 0);

                    next_state = EVT_MGR_STATE_CONNECTING;
                }
                break;

            case EVT_MGR_STATE_CONNECTING:
                if (msg.type == IPC_MSG_CONNECTION_SUCCESS) {
                    LOG_INFO("Connection succeeded!");

                    dec_msg.type = IPC_MSG_DECODER_START;
                    xQueueSend(ipc->decoder_q, &dec_msg, 0);

                    next_state = EVT_MGR_STATE_STARTING_DECODER;
                }
                else if (msg.type == IPC_MSG_CONNECTION_FAIL) {
                    LOG_FATAL("Connection failed!");
                    configASSERT(0); // TODO at this development stage it's fatal
                }
                break;

            case EVT_MGR_STATE_STARTING_DECODER:
                if (msg.type == IPC_MSG_DECODER_RUNNING) {
                    LOG_INFO("Decoder running, sample rate %dHz!", (uint32_t)msg.arg);

                    player_msg.type = IPC_MSG_PLAYER_START;
                    player_msg.arg = (uint32_t)msg.arg; // Sample rate
                    xQueueSend(ipc->player_q, &player_msg, 0);

                    next_state = EVT_MGR_STATE_STARTING_PLAYER;
                }
                else if (msg.type == IPC_MSG_DECODER_FAIL) {
                    LOG_FATAL("Decoder failed!");
                    configASSERT(0); // TODO at this development stage it's fatal
                }
                break;

            case EVT_MGR_STATE_STARTING_PLAYER:
                if (msg.type == IPC_MSG_PLAYER_RUNNING) {
                    LOG_INFO("Player running!");

                    next_state = EVT_MGR_STATE_PLAYBACK_RUNNING;
                }
                // TODO stop, link down etc in all other states too
                break;

            case EVT_MGR_STATE_PLAYBACK_RUNNING:
                LOG_INFO("Message: %d", msg.type);
                if (msg.type == IPC_MSG_NETWORK_LINK_DOWN) {

                    player_msg.type = IPC_MSG_PLAYER_STOP;
                    xQueueSend(ipc->player_q, &player_msg, 0);

                    dec_msg.type = IPC_MSG_DECODER_STOP;
                    xQueueSend(ipc->decoder_q, &dec_msg, 0);

                    conn_msg.type = IPC_MSG_CONNECTION_STOP;
                    xQueueSend(ipc->conn_q, &conn_msg, 0);

                    next_state = EVT_MGR_STATE_GET_LINK;
                }
                else if (msg.type == IPC_MSG_CONNECTION_FAIL) {
                    player_msg.type = IPC_MSG_PLAYER_STOP;
                    xQueueSend(ipc->player_q, &player_msg, 0);

                    dec_msg.type = IPC_MSG_DECODER_STOP;
                    xQueueSend(ipc->decoder_q, &dec_msg, 0);

                    conn_msg.type = IPC_MSG_CONNECTION_START;
                    conn_msg.port = 8904;
                    conn_msg.host = "mp3.polskieradio.pl";
                    xQueueSend(ipc->conn_q, &conn_msg, 0);

                    next_state = EVT_MGR_STATE_CONNECTING;
                }
                break;

            default:
                LOG_ERROR("Invalid state: %d", state);
                break;
        }

        if (state != next_state) {
            LOG_INFO("%s -> %s", evt_mgr_state_str[state], evt_mgr_state_str[next_state]);
            state = next_state;
        }
    }
}

void event_manager_init(void)
{
    xTaskCreate(event_manager_task,
                EVT_MGR_TASK_NAME,
                EVT_MGR_TASK_STACK_SIZE,
                NULL,
                EVT_MGR_TASK_PRIO,
                NULL);
}
