#include "event_manager.h"
#include <FreeRTOS.h>
#include <task.h>
#include <utils.h>
#include <ipc_context.h>
#include <ipc_message.h>
#include <logger.h>

#define EVT_MGR_TASK_NAME "event_manager"
#define EVT_MGR_TASK_STACK_SIZE UTILS_STACK_BYTES_TO_WORDS(1024 * 2)
#define EVT_MGR_TASK_PRIO 1
#define EVT_MGR_TASK_CORE_AFFINITY 0

typedef enum
{
    EVT_MGR_STATE_WAIT_LINK_UP,
    EVT_MGR_STATE_WAIT_IP,
    EVT_MGR_STATE_WAIT_CONN,
    EVT_MGR_STATE_WAIT_DEC_READY,
    EVT_MGR_STATE_WAIT_DEC_RUNNING,
    EVT_MGR_STATE_WAIT_PLAYER_RUNNING,
} evt_mgr_state_t;

typedef struct 
{
    evt_mgr_state_t state;
} evt_mgr_ctx_t;

static evt_mgr_ctx_t ctx;

static void event_manager_task(void *arg)
{
    const ipc_ctx_t *ipc = ipc_context_get();

    ipc_manager_msg_t msg;
    ipc_connection_msg_t conn_msg;
    ipc_decoder_msg_t dec_msg;
    ipc_player_msg_t player_msg;

    uint16_t sample_rate;

    while (1) {
        xQueueReceive(ipc->manager_q, &msg, portMAX_DELAY);

        // vTaskDelay(1000); // Wait before each transition

        switch (ctx.state) {
            case EVT_MGR_STATE_WAIT_LINK_UP:
                if (msg.type == IPC_MSG_NETWORK_LINK_UP) {
                    LOG_INFO("Link UP!");
                    ctx.state = EVT_MGR_STATE_WAIT_IP;
                }
                break;

            case EVT_MGR_STATE_WAIT_IP:
                if (msg.type == IPC_MSG_NETWORK_GOT_IP) {
                    LOG_INFO("Got IP!");

                    conn_msg.type = IPC_MSG_CONNECTION_START;
                    conn_msg.port = 8904;
                    conn_msg.host = "mp3.polskieradio.pl";
                    xQueueSend(ipc->conn_q, &conn_msg, 0);

                    ctx.state = EVT_MGR_STATE_WAIT_CONN;
                }
                else if (msg.type == IPC_MSG_NETWORK_LINK_DOWN) {
                    LOG_INFO("Link DOWN!");
                    ctx.state = EVT_MGR_STATE_WAIT_LINK_UP;
                }
                break;

            case EVT_MGR_STATE_WAIT_CONN:
                if (msg.type == IPC_MSG_CONNECTION_FAIL) {
                    LOG_FATAL("Connection failed!");
                    configASSERT(0); // TODO at this development stage it's fatal
                }
                else if (msg.type == IPC_MSG_CONNECTION_SUCCESS) {
                    LOG_INFO("Connection succeeded!");

                    dec_msg.type = IPC_MSG_DECODER_INIT;
                    xQueueSend(ipc->decoder_q, &dec_msg, 0);

                    ctx.state = EVT_MGR_STATE_WAIT_DEC_READY;
                }
                break;

            case EVT_MGR_STATE_WAIT_DEC_READY:
                if (msg.type == IPC_MSG_DECODER_FAIL) {
                    LOG_FATAL("Decoder init failed!");
                    configASSERT(0); // TODO at this development stage it's fatal
                }
                else if (msg.type == IPC_MSG_DECODER_READY) {
                    sample_rate = (uint16_t)msg.arg;

                    LOG_INFO("Decoder ready, sample rate: %d!", (int)sample_rate);

                    dec_msg.type = IPC_MSG_DECODER_START;
                    xQueueSend(ipc->decoder_q, &dec_msg, 0);

                    ctx.state = EVT_MGR_STATE_WAIT_DEC_RUNNING;
                }
                break;

            case EVT_MGR_STATE_WAIT_DEC_RUNNING:
                if (msg.type == IPC_MSG_DECODER_RUNNING) {
                    LOG_INFO("Decoder running!");

                    player_msg.type = IPC_MSG_PLAYER_START;
                    player_msg.arg = sample_rate;
                    xQueueSend(ipc->player_q, &player_msg, 0);
                }
                break;

            case EVT_MGR_STATE_WAIT_PLAYER_RUNNING:
                if (msg.type == IPC_MSG_PLAYER_RUNNING) {
                    LOG_INFO("Player running!");
                }
                break;

            default:
                LOG_ERROR("Invalid state: %d", ctx.state);
                break;
        }
    }
}

void event_manager_init(void)
{
    // xTaskCreateAffinitySet(event_manager_task, 
    //                        EVT_MGR_TASK_NAME,
    //                        EVT_MGR_TASK_STACK_SIZE,
    //                        NULL,
    //                        EVT_MGR_TASK_PRIO,
    //                        1 << EVT_MGR_TASK_CORE_AFFINITY,
    //                        NULL);

    xTaskCreate(event_manager_task, 
                           EVT_MGR_TASK_NAME,
                           EVT_MGR_TASK_STACK_SIZE,
                           NULL,
                           EVT_MGR_TASK_PRIO,
                           NULL);
}
