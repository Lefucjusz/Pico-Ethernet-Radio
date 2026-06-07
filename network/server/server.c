#include "server.h"
#include "webpage.h"
#include <FreeRTOS.h>
#include <task.h>
#include <lwip/sockets.h>
#include <utils.h>
#include <logger.h>
#include <ipc_context.h>
#include <ipc_message.h>

#define SERVER_TASK_NAME "server"
#define SERVER_TASK_STACK_SIZE UTILS_STACK_BYTES_TO_WORDS(1024 * 1)
#define SERVER_TASK_PRIO 1

static int server_create(void)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return -1;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(80),
        .sin_addr.s_addr = INADDR_ANY
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        return -1;
    }

    if (listen(sock, 2) < 0) {
        return -1;
    }

    return sock;
}

static void server_send_response(int client, const char *resp, size_t size)
{
    static char tx_buf[1024];

    size_t bytes_sent = 0;
    while (bytes_sent < size) {
        const size_t bytes_to_send = UTILS_MIN(size - bytes_sent, sizeof(tx_buf));

        send(client, &resp[bytes_sent], bytes_to_send, 0);

        bytes_sent += bytes_to_send;
    }
}

static void server_send_page(int client)
{
    static char tmp[128];

    snprintf(tmp,
            sizeof(tmp),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: %u\r\n"
            "Connection: close\r\n"
            "\r\n",
            (unsigned)strlen(webpage)
    );

    server_send_response(client, tmp, strlen(tmp));
    server_send_response(client, webpage, strlen(webpage));
}

static int server_parse_value(const char *req)
{
    const char *q = strchr(req, '?');
    if (q == NULL) {
        return 0;
    }
    ++q;

    const char *end = strchr(q, ' ');
    if (end == NULL) {
        return 0;
    }

    if (strncmp(q, "value=", 6) != 0) {
        return 0;
    }

    return atoi(q + 6);
}

static void server_handle_request(int client, const char *req)
{
    if (strncmp(req, "GET / ", 6) == 0) { // TODO magic numbers
        server_send_page(client);
    }
    else if (strncmp(req, "GET /api/volume", 15) == 0) {
        const uint16_t volume = server_parse_value(req);

        // LOG_DEBUG("Got volume request, value: %d", volume);
        
        ipc_player_msg_t msg = {.type = IPC_MSG_PLAYER_SET_VOLUME, .arg = volume};
        xQueueSend(ipc_context_get()->player_q, &msg, 0);
    }
}

static int server_receive_request(int client, char *buf, size_t buf_size)
{
    size_t total = 0;

    while (total < (buf_size - 1)) {
        const int len = recv(client, &buf[total], buf_size - 1 - total, 0);
        if (len < 0) {
            return -1;
        }
        if (len == 0) {
            break;
        }

        total += len;
        buf[total] = '\0';

        if (strstr(buf, "\r\n\r\n") != NULL) {
            return total;
        }
    }

    return (total == 0) ? 0 : -1;
}

static void server_task(void *arg)
{
    LOG_INFO("Started at core %d", portGET_CORE_ID());

    int sock = server_create();
    if (sock < 0) {
        LOG_FATAL("Failed to create HTTP server!");
        configASSERT(0);
    }
    
    struct sockaddr_in addr;
    static char buf[1024];

    while (1) {
        socklen_t addr_len = sizeof(addr);

        int client = accept(sock, (struct sockaddr *)&addr, &addr_len);
        if (client >= 0) {
            // LOG_INFO("Got client!");

            int len = server_receive_request(client, buf, sizeof(buf));
            if (len <= 0) {
                LOG_ERROR("Failed to receive request!");
            }
            else {
                server_handle_request(client, buf);
            }

            close(client);
        }
    }
}

void server_init(void)
{
    xTaskCreate(server_task,
                SERVER_TASK_NAME,
                SERVER_TASK_STACK_SIZE,
                NULL,
                SERVER_TASK_PRIO,
                NULL);
}
