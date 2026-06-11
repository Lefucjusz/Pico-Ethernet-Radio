#include "event_manager.h"
#include <FreeRTOS.h>
#include <task.h>
#include <timers.h>
#include <ipc_context.h>
#include <ipc_message.h>
#include <radio_state.h>
#include <utils.h>
#include <logger.h>
#include <stdlib.h>

#define EVT_MGR_TASK_NAME "event_manager"
#define EVT_MGR_TASK_STACK_SIZE UTILS_STACK_BYTES_TO_WORDS(1024 * 1)
#define EVT_MGR_TASK_PRIO 1

#define EVT_MGR_RESTART_TIMER_NAME "restart_timer"
#define EVT_MGR_RESTART_TIMER_BASE_PERIOD_TICKS pdMS_TO_TICKS(1000)
#define EVT_MGR_RESTART_MAX_ATTEMPTS 4

typedef struct
{
    const ipc_ctx_t *ipc;
    TimerHandle_t restart_timer;
    size_t restart_attempts;
} evt_mgr_ctx_t;

static evt_mgr_ctx_t ctx;

static void evt_mgr_stream_start(const char *url)
{
    ipc_stream_msg_t msg = {
        .type = IPC_MSG_STREAM_START,
        .url = (void *)url
    };
    xQueueSend(ctx.ipc->stream_q, &msg, 0);
}

static void evt_mgr_stream_stop(void)
{
    ipc_stream_msg_t msg = {
        .type = IPC_MSG_STREAM_STOP
    };
    xQueueSend(ctx.ipc->stream_q, &msg, 0);
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

static void evt_mgr_restart_timer_callback(TimerHandle_t t)
{
    ipc_manager_msg_t msg = {
        .type = IPC_MSG_MANAGER_RESTART_STREAM
    };
    xQueueSend(ctx.ipc->manager_q, &msg, 0);
}

static bool evt_mgr_restart_reschedule(void)
{
    if (ctx.restart_attempts >= EVT_MGR_RESTART_MAX_ATTEMPTS) {
        LOG_ERROR("Restart attempts limit reached!");
        return false;
    }

    LOG_INFO("Restarting (%u/%u)...", ctx.restart_attempts + 1, EVT_MGR_RESTART_MAX_ATTEMPTS);

    const TickType_t restart_period = EVT_MGR_RESTART_TIMER_BASE_PERIOD_TICKS << ctx.restart_attempts; // Exponential backoff
    LOG_DEBUG("Restart delay: %ums", pdTICKS_TO_MS(restart_period));
    xTimerChangePeriod(ctx.restart_timer, restart_period, 0);
    ++ctx.restart_attempts;

    return true;
}

static void evt_mgr_restart_abort(void)
{
    xTimerStop(ctx.restart_timer, 0);
}

static void evt_mgr_task(void *arg)
{
    radio_status_t status = {0};
    radio_state_t new_state;
    ipc_manager_msg_t msg;

    ctx.ipc = ipc_context_get();
    ctx.restart_timer = xTimerCreate(EVT_MGR_RESTART_TIMER_NAME,
                                     EVT_MGR_RESTART_TIMER_BASE_PERIOD_TICKS,
                                     pdFALSE,
                                     NULL,
                                     evt_mgr_restart_timer_callback);

    status.volume = 50;

    LOG_INFO("Started at core %d", portGET_CORE_ID());

    while (1) {
        xQueueReceive(ctx.ipc->manager_q, &msg, portMAX_DELAY);

        if (msg.type == IPC_MSG_UI_GET_STATUS) {
            evt_mgr_server_send_status(&status);
            continue;
        }

        new_state = status.state;
        switch (status.state) { // TODO add missing state transitions
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

                    case IPC_MSG_NETWORK_LINK_DOWN:
                        LOG_INFO("Link DOWN!");
                        new_state = RADIO_STATE_GET_LINK;
                        break;

                    default:
                        break;
                }
                break;

            case RADIO_STATE_READY:
                switch (msg.type) {
                    case IPC_MSG_UI_START_PLAYBACK:
                        strlcpy(status.stream_url, msg.arg, sizeof(status.stream_url));
                        evt_mgr_stream_start(status.stream_url);
                        new_state = RADIO_STATE_STARTING_STREAM;
                        break;

                    case IPC_MSG_UI_SET_VOLUME:
                        status.volume = (uintptr_t)msg.arg;
                        evt_mgr_player_set_volume(status.volume);
                        break;

                    case IPC_MSG_NETWORK_LINK_DOWN:
                        LOG_INFO("Link DOWN!");
                        new_state = RADIO_STATE_GET_LINK;
                        break;

                    default:
                        break;
                }
                break;

            case RADIO_STATE_STARTING_STREAM:
                switch (msg.type) {
                    case IPC_MSG_STREAM_RUNNING:
                        LOG_INFO("Stream established!");
                        evt_mgr_decoder_start();
                        new_state = RADIO_STATE_STARTING_DECODER;
                        break;

                    case IPC_MSG_STREAM_FAIL:
                        LOG_ERROR("Stream failed!");
                        if (evt_mgr_restart_reschedule()) {
                            new_state = RADIO_STATE_AWAITING_RESTART;
                        }
                        else {
                            new_state = RADIO_STATE_ERROR;
                        }
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
                        LOG_ERROR("Decoder failed!");
                        evt_mgr_stream_stop();
                        if (evt_mgr_restart_reschedule()) {
                            new_state = RADIO_STATE_AWAITING_RESTART;
                        }
                        else {
                            new_state = RADIO_STATE_ERROR;
                        }
                        break;

                    default:
                        break;
                }
                break;

            case RADIO_STATE_STARTING_PLAYER:
                switch (msg.type) {
                    case IPC_MSG_PLAYER_RUNNING:
                        LOG_INFO("Player running!");
                        ctx.restart_attempts = 0;
                        new_state = RADIO_STATE_PLAYBACK_RUNNING;
                        break;

                    default:
                        break;
                    }
                break;

            case RADIO_STATE_PLAYBACK_RUNNING:
                switch (msg.type) {
                    case IPC_MSG_NETWORK_LINK_DOWN:
                        evt_mgr_player_stop();
                        evt_mgr_decoder_stop();
                        evt_mgr_stream_stop();
                        new_state = RADIO_STATE_GET_LINK;
                        break;

                    case IPC_MSG_STREAM_FAIL:
                        LOG_ERROR("Stream failed!");
                        evt_mgr_player_stop();
                        evt_mgr_decoder_stop();
                        if (evt_mgr_restart_reschedule()) {
                            new_state = RADIO_STATE_AWAITING_RESTART;
                        }
                        else {
                            new_state = RADIO_STATE_ERROR;
                        }
                        break;

                    case IPC_MSG_UI_SET_VOLUME:
                        status.volume = (uintptr_t)msg.arg;
                        evt_mgr_player_set_volume(status.volume);
                        break;

                    case IPC_MSG_UI_STOP_PLAYBACK:
                        evt_mgr_player_stop();
                        evt_mgr_decoder_stop();
                        evt_mgr_stream_stop();
                        new_state = RADIO_STATE_READY;
                        break;

                    default:
                        LOG_INFO("Unhandled message: %d", msg.type);
                        break;
                }
                break;

            case RADIO_STATE_AWAITING_RESTART:
                switch (msg.type) {
                    case IPC_MSG_MANAGER_RESTART_STREAM:
                        evt_mgr_stream_start(status.stream_url);
                        new_state = RADIO_STATE_STARTING_STREAM;
                        break;

                    case IPC_MSG_NETWORK_LINK_DOWN:
                        evt_mgr_restart_abort();
                        new_state = RADIO_STATE_GET_LINK;
                        break;

                    case IPC_MSG_UI_STOP_PLAYBACK:
                        evt_mgr_restart_abort();
                        new_state = RADIO_STATE_READY;
                        break;

                    default:
                        break;
                }
                break;

            case RADIO_STATE_ERROR:
                switch (msg.type) {
                    case IPC_MSG_NETWORK_LINK_DOWN:
                        new_state = RADIO_STATE_GET_LINK;
                        break;

                    case IPC_MSG_UI_STOP_PLAYBACK:
                        ctx.restart_attempts = 0;
                        new_state = RADIO_STATE_READY;
                        break;

                    case IPC_MSG_UI_START_PLAYBACK:
                        ctx.restart_attempts = 0;
                        strlcpy(status.stream_url, msg.arg, sizeof(status.stream_url));
                        evt_mgr_stream_start(status.stream_url);
                        new_state = RADIO_STATE_STARTING_STREAM;
                        break;

                    case IPC_MSG_UI_SET_VOLUME:
                        status.volume = (uintptr_t)msg.arg;
                        evt_mgr_player_set_volume(status.volume);
                        break;
                }
                break;

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
