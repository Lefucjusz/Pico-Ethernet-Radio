#include "event_manager.h"
#include <FreeRTOS.h>
#include <task.h>
#include <ipc_context.h>
#include <ipc_message.h>
#include <radio_state.h>
#include <utils.h>
#include <logger.h>
#include <stdlib.h>

#define EVT_MGR_TASK_NAME "event_manager"
#define EVT_MGR_TASK_STACK_SIZE UTILS_STACK_BYTES_TO_WORDS(1024 * 1)
#define EVT_MGR_TASK_PRIO 1

typedef struct
{
    const ipc_ctx_t *ipc;
} evt_mgr_ctx_t;

static evt_mgr_ctx_t ctx;

static bool evt_mgr_parse_url(const char *url, radio_url_t*out)
{
    /* Skip scheme */
    const char *host_start = strstr(url, "://");
    host_start = (host_start != NULL) ? (host_start + 3) : url;

    /* Find path */
    const char *path_start = strchr(host_start, '/');
    const char *host_end = (path_start != NULL) ? path_start : (host_start + strlen(host_start));

    /* Copy path */
    if (path_start != NULL) {
        const size_t path_len = strlen(path_start);
        if (path_len >= sizeof(out->path)) {
            return false;
        }
        memcpy(out->path, path_start, path_len);
        out->path[path_len] = '\0';
    }
    else {
        strcpy(out->path, "/");
    }

    /* Extract host and port */
    const char *port_start = strchr(host_start, ':');
    if (port_start != NULL) {
        const size_t host_len = port_start - host_start;
        if (host_len >= sizeof(out->host)) {
            return false;
        }

        memcpy(out->host, host_start, host_len);
        out->host[host_len] = '\0';

        out->port = (uint16_t)atoi(port_start + 1);
    }
    else {
        const size_t host_len = host_end - host_start;
        if (host_len >= sizeof(out->host)) {
            return false;
        }

        memcpy(out->host, host_start, host_len);
        out->host[host_len] = '\0';

        out->port = 80;
    }

    return true;
}

static void evt_mgr_connection_start(const radio_url_t *url)
{
    ipc_connection_msg_t msg = {
        .type = IPC_MSG_CONNECTION_START,
        .url = url
    };
    xQueueSend(ctx.ipc->conn_q, &msg, 0);
}

static void evt_mgr_connection_stop(void)
{
    ipc_connection_msg_t msg = {
        .type = IPC_MSG_CONNECTION_STOP
    };
    xQueueSend(ctx.ipc->conn_q, &msg, 0);
}

static void evt_mgr_decoder_start(void)
{
    ipc_decoder_msg_t msg = {
        .type = IPC_MSG_DECODER_START
    };
    xQueueSend(ctx.ipc->decoder_q, &msg, 0);
}

static void evt_mgr_decoder_stop(void)
{
    ipc_decoder_msg_t msg = {
        .type = IPC_MSG_DECODER_STOP
    };
    xQueueSend(ctx.ipc->decoder_q, &msg, 0);
}

static void evt_mgr_player_set_volume(uint8_t volume)
{
    ipc_player_msg_t msg = {
        .type = IPC_MSG_PLAYER_SET_VOLUME,
        .arg = volume
    };
    xQueueSend(ctx.ipc->player_q, &msg, 0);
}

static void evt_mgr_player_start(uint16_t sample_rate)
{
    ipc_player_msg_t msg = {
        .type = IPC_MSG_PLAYER_START,
        .arg = sample_rate
    };
    xQueueSend(ctx.ipc->player_q, &msg, 0);
}

static void evt_mgr_player_stop(void)
{
    ipc_player_msg_t msg = {
        .type = IPC_MSG_PLAYER_STOP
    };
    xQueueSend(ctx.ipc->player_q, &msg, 0);
}

static void evt_mgr_server_send_status(const radio_status_t *status)
{
    ipc_server_msg_t msg = {
        .type = IPC_MSG_UI_STATUS,
        .status = *status
    };
    xQueueSend(ctx.ipc->server_q, &msg, 0);
}

static void evt_mgr_task(void *arg)
{
    ctx.ipc = ipc_context_get();

    ipc_manager_msg_t msg;

    radio_status_t status = {0};
    radio_state_t new_state;

    status.volume = 50;

    LOG_INFO("Started at core %d", portGET_CORE_ID());

    while (1) {
        xQueueReceive(ctx.ipc->manager_q, &msg, portMAX_DELAY);

        if (msg.type == IPC_MSG_UI_GET_STATUS) {
            evt_mgr_server_send_status(&status);
            continue;
        }

        switch (status.state) {
            case RADIO_STATE_GET_LINK:
                switch (msg.type) {
                    case IPC_MSG_NETWORK_LINK_UP:
                        LOG_INFO("Link UP!");
                        new_state = RADIO_STATE_GET_IP;
                        break;

                    default:
                        break;
                }
                break;

            case RADIO_STATE_GET_IP:
               switch (msg.type) {
                    case IPC_MSG_NETWORK_GOT_IP:
                        LOG_INFO("Got IP!");
                        new_state = RADIO_STATE_READY;
                        break;

                    default:
                        break;
                }
                break;

            case RADIO_STATE_READY:
                switch (msg.type) {
                    case IPC_MSG_UI_START_PLAYBACK:
                        evt_mgr_parse_url(msg.arg, &status.stream_url);
                        evt_mgr_connection_start(&status.stream_url);
                        new_state = RADIO_STATE_CONNECTING;
                        break;

                    case IPC_MSG_UI_SET_VOLUME:
                        status.volume = (uintptr_t)msg.arg;
                        evt_mgr_player_set_volume(status.volume);
                        break;

                    default:
                        break;
                }
                break;

            case RADIO_STATE_CONNECTING:
                switch (msg.type) {
                    case IPC_MSG_CONNECTION_SUCCESS:
                        LOG_INFO("Connection succeeded!");
                        evt_mgr_decoder_start();
                        new_state = RADIO_STATE_STARTING_DECODER;
                        break;

                    case IPC_MSG_CONNECTION_FAIL:
                        LOG_FATAL("Connection failed!");
                        configASSERT(0); // TODO at this development stage it's fatal
                        break;

                    default:
                        break;
                }
                break;

            case RADIO_STATE_STARTING_DECODER:
                switch (msg.type) {
                    case IPC_MSG_DECODER_RUNNING:
                        LOG_INFO("Decoder running, sample rate %uHz!", (uintptr_t)msg.arg);
                        evt_mgr_player_start((uintptr_t)msg.arg);
                        new_state = RADIO_STATE_STARTING_PLAYER;
                        break;

                    case IPC_MSG_DECODER_FAIL:
                        LOG_FATAL("Decoder failed!");
                        configASSERT(0); // TODO at this development stage it's fatal
                        break;

                    default:
                        break;
                }
                break;

            case RADIO_STATE_STARTING_PLAYER:
                switch (msg.type) {
                    case IPC_MSG_PLAYER_RUNNING:
                        LOG_INFO("Player running!");
                        new_state = RADIO_STATE_PLAYBACK_RUNNING;
                        break;

                    default:
                        break;
                    }
                    // TODO stop, link down etc in all other states too
                break;

            case RADIO_STATE_PLAYBACK_RUNNING:
                LOG_INFO("Message: %d", msg.type);
                switch (msg.type) {
                    case IPC_MSG_NETWORK_LINK_DOWN:
                        evt_mgr_player_stop();
                        evt_mgr_decoder_stop();
                        evt_mgr_connection_stop();
                        new_state = RADIO_STATE_GET_LINK;
                        break;

                    case IPC_MSG_CONNECTION_FAIL:
                        evt_mgr_player_stop();
                        evt_mgr_decoder_stop();
                        evt_mgr_connection_start(&status.stream_url); // Try to restart the connection

                        // TODO retry count, backoff

                        new_state = RADIO_STATE_CONNECTING;
                        break;

                    case IPC_MSG_UI_SET_VOLUME:
                        status.volume = (uintptr_t)msg.arg;
                        evt_mgr_player_set_volume(status.volume);
                        break;

                    case IPC_MSG_UI_STOP_PLAYBACK:
                        evt_mgr_player_stop();
                        evt_mgr_decoder_stop();
                        evt_mgr_connection_stop();
                        new_state = RADIO_STATE_READY;
                        break;

                    default:
                        break;
                }

            default:
                break;
        }

        if (status.state != new_state) {
            LOG_INFO("%s -> %s", radio_state_get_name(status.state), radio_state_get_name(new_state));
            status.state = new_state;
        }
    }
}

void event_manager_init(void)
{
    xTaskCreate(evt_mgr_task,
                EVT_MGR_TASK_NAME,
                EVT_MGR_TASK_STACK_SIZE,
                NULL,
                EVT_MGR_TASK_PRIO,
                NULL);
}
