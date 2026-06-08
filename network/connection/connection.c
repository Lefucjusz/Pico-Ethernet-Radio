#include "connection.h"
#include <ipc_context.h>
#include <ipc_message.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <utils.h>
#include <logger.h>

#define CONN_TASK_NAME "connection"
#define CONN_TASK_STACK_SIZE UTILS_STACK_BYTES_TO_WORDS(1024 * 1)
#define CONN_TASK_PRIO 1

static uint8_t rx_buffer[2048];

static int connection_connect(const radio_url_t *url) // TODO non-blocking?
{
    struct hostent *he;
    struct sockaddr_in addr;
    int err;
    int sock;

    /* Resolve DNS */
    he = gethostbyname(url->host);
    if (he == NULL) {
        LOG_ERROR("DNS failed for host: %s", url->host);
        return -1;
    }

    /* Create socket */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        LOG_ERROR("Failed to create socket");
        return -1;
    }

    /* Set address and port */
    memset(&addr, 0, sizeof(addr));
    memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
    addr.sin_port = htons(url->port);
    addr.sin_family = AF_INET;

    /* Connect */
    err = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    if (err < 0) {
        LOG_ERROR("Failed to connect, error %d", err);
        closesocket(sock);
        return -1;
    }

    struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* Send request */
    char request[128];
    snprintf(request, sizeof(request), "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: lwip-radio\r\nIcy-MetaData: 0\r\n\r\n", url->path, url->host);
    if (send(sock, request, strlen(request), 0) != strlen(request)) {
        LOG_ERROR("Failed to send request!");
        closesocket(sock);
        return -1;
    }

    return sock;
}

static void connection_disconnect(int *sock)
{
    shutdown(*sock, SHUT_RDWR);
    closesocket(*sock);
    *sock = -1;
}

static void connection_task(void *arg)
{
    const ipc_ctx_t *ipc = ipc_context_get();
    ipc_connection_msg_t conn_msg;
    ipc_manager_msg_t manager_msg;
    int sock = -1;
    int len;

    bool header = false;

    LOG_INFO("Started at core %d", portGET_CORE_ID());

    while (1) {
        /* Idle: wait for connection request */
        if (sock < 0) {
            xQueueReceive(ipc->conn_q, &conn_msg, portMAX_DELAY);
            if (conn_msg.type != IPC_MSG_CONNECTION_START) {
                LOG_ERROR("Got invalid message type: %d", conn_msg.type);
                continue;
            }

            sock = connection_connect(conn_msg.url);
            if (sock < 0) {
                manager_msg.type = IPC_MSG_CONNECTION_FAIL;
            }
            else {
                manager_msg.type = IPC_MSG_CONNECTION_SUCCESS;
            }
            xQueueSend(ipc->manager_q, &manager_msg, 0);
        }

        /* Streaming: non-blocking command check */
        if (xQueueReceive(ipc->conn_q, &conn_msg, 0) == pdTRUE) {
            if (conn_msg.type != IPC_MSG_CONNECTION_STOP) {
                LOG_ERROR("Got invalid message type: %d", conn_msg.type);
            }
            else {
                connection_disconnect(&sock);
                manager_msg.type = IPC_MSG_CONNECTION_STOPPED;
                xQueueSend(ipc->manager_q, &manager_msg, 0);
            }
        }

        /* TODO fix this abhorrency */
        /* TODO this runs even if connect() fails, fix this */
        if (!header) {
        restart:
            size_t bytes = 0;
            while (bytes < 256) {
                len = recv(sock, &rx_buffer[bytes], sizeof(rx_buffer) - bytes, 0);
                if (len <= 0) {
                    configASSERT(0);
                }
                bytes += len;
            }

            char *p = strstr(rx_buffer, "\r\n\r\n");
            if (!p) {
                LOG_ERROR("no header!");
                goto restart;
            }
            p += 4;

            size_t data_offset = p - (char *)rx_buffer;
            size_t data_len = bytes - data_offset;

            memmove(rx_buffer, p, data_len);
            xStreamBufferSend(ipc->recv_buffer, rx_buffer, data_len, 1000);

            header = true;

        }
        else {

            /* Streaming: receive data */
            len = recv(sock, rx_buffer, sizeof(rx_buffer), 0);
            if (len > 0) {
                // LOG_DEBUG("Received %d bytes", len);
                // vTaskDelay(10);
               xStreamBufferSend(ipc->recv_buffer, rx_buffer, len, 1000); // TODO magic numbers
            }
            else {
                LOG_WARN("Connection lost!");
                connection_disconnect(&sock);
                xStreamBufferReset(ipc->recv_buffer);

                header = false;

                manager_msg.type = IPC_MSG_CONNECTION_FAIL;
                xQueueSend(ipc->manager_q, &manager_msg, 0);
            }
        }
    }
}

void connection_init(void)
{
    xTaskCreate(connection_task,
                CONN_TASK_NAME,
                CONN_TASK_STACK_SIZE,
                NULL,
                CONN_TASK_PRIO,
                NULL);
}
