#include "stream.h"
#include <ipc_context.h>
#include <ipc_message.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <utils.h>
#include <logger.h>
#include <errno.h>

#define STREAM_TASK_NAME "stream"
#define STREAM_TASK_STACK_SIZE UTILS_STACK_BYTES_TO_WORDS(1024 * 1)
#define STREAM_TASK_PRIO 1

#define STREAM_HTTP_DEFAULT_PORT 80
#define STREAM_RECV_TIMEOUT_S 5
#define STREAM_BUFFER_SEND_TIMEOUT_TICKS pdMS_TO_TICKS(1000)

#define STREAM_HTTP_STATUS_OK 200

typedef enum
{
    STREAM_IDLE,
    STREAM_PARSING_HEADER,
    STREAM_RUNNING
} stream_state_t;

typedef struct
{
    char host[64];
    char path[64];
    uint16_t port;
} stream_url_t;

static const ipc_ctx_t *ipc;

static bool stream_parse_url(const char *url, stream_url_t *out)
{
    /* Check if stream is not HTTPS */
    const char *https = strstr(url, "https://");
    if (https != NULL) {
        LOG_ERROR("HTTPS not supported");
        return false;
    }

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
            LOG_ERROR("Path buffer overflow");
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
            LOG_ERROR("Host buffer overflow");
            return false;
        }

        memcpy(out->host, host_start, host_len);
        out->host[host_len] = '\0';

        out->port = (uint16_t)atoi(port_start + 1);
    }
    else {
        const size_t host_len = host_end - host_start;
        if (host_len >= sizeof(out->host)) {
            LOG_ERROR("Host buffer overflow");
            return false;
        }

        memcpy(out->host, host_start, host_len);
        out->host[host_len] = '\0';

        out->port = STREAM_HTTP_DEFAULT_PORT;
    }

    return true;
}

static int stream_connect(const stream_url_t *url, uint8_t *req_buf, size_t req_buf_size)
{
    struct hostent *he;
    struct sockaddr_in addr;
    int sock;
    int err;

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

    /* Set receive timeout */
    const struct timeval tv = { 
        .tv_sec = STREAM_RECV_TIMEOUT_S, 
        .tv_usec = 0
    };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

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

    /* Create request */
    snprintf(req_buf,
             req_buf_size, 
             "GET %s HTTP/1.0\r\n"
             "Host: %s\r\n"
             "User-Agent: lwip-radio\r\n"
             "Icy-MetaData: 0\r\n" // TODO support metadata
             "\r\n",
             url->path,
             url->host
    );

    /* Send request */
    const size_t req_size = strlen(req_buf);
    if (send(sock, req_buf, req_size, 0) != req_size) {
        LOG_ERROR("Failed to send GET request");
        closesocket(sock);
        return -1;
    }

    return sock;
}

static void stream_disconnect(int *sock)
{
    shutdown(*sock, SHUT_RDWR);
    closesocket(*sock);
    *sock = -1;
}

static uint8_t *stream_find_body_start(uint8_t *data, size_t size)
{
    for (size_t i = 0; (i + 3) < size; ++i) {
        if (memcmp(&data[i], "\r\n\r\n", 4) == 0) {
            return &data[i + 4];
        }
    }
    return NULL;
}

static bool stream_header_check(const uint8_t *header)
{
    /* Validate header start */
    if ((strncmp(header, "HTTP/", 5) != 0) && (strncmp(header, "ICY ", 4) != 0)) {
        LOG_ERROR("Invalid header start!");
        return false;
    }

    /* Validate status code */
    const char *code_start = strchr(header, ' ');
    if (code_start == NULL) {
        LOG_ERROR("HTTP status code not found");
        return false;
    }
    const int code = atoi(code_start + 1);
    if (code != STREAM_HTTP_STATUS_OK) {
        LOG_ERROR("Expected HTTP code 200, got %d", code);
        return false;
    }

    /* Validate content type */
    if (strstr(header, "audio/mpeg") == NULL) {
        LOG_ERROR("Invalid content type");
        return false;
    }

    return true;
}

static TickType_t stream_get_queue_block_time(stream_state_t state)
{
    if (state == STREAM_IDLE) {
        return portMAX_DELAY;
    }
    return 0;
}

static void stream_report_running(void)
{
    ipc_manager_msg_t msg = {
        .type = IPC_MSG_STREAM_RUNNING
    };
    xQueueSend(ipc->manager_q, &msg, 0);
}

static void stream_report_stopped(void)
{
    ipc_manager_msg_t msg = {
        .type = IPC_MSG_STREAM_STOPPED
    };
    xQueueSend(ipc->manager_q, &msg, 0);
}

static void stream_report_fail(void)
{
    ipc_manager_msg_t msg = {
        .type = IPC_MSG_STREAM_FAIL
    };
    xQueueSend(ipc->manager_q, &msg, 0);
}

static void stream_task(void *arg)
{
    static uint8_t http_buf[2048];

    stream_state_t state;
    stream_url_t url;
    ipc_stream_msg_t msg;
    size_t header_block_size;
    int sock;
    
    ipc = ipc_context_get();

    LOG_INFO("Started at core %d", portGET_CORE_ID());

    while (1) {
        /* Handle message-triggered state transitions */
        if (xQueueReceive(ipc->stream_q, &msg, stream_get_queue_block_time(state)) == pdTRUE) {
            switch (state) {
                case STREAM_IDLE:
                    if (msg.type == IPC_MSG_STREAM_START) {
                        if (!stream_parse_url(msg.url, &url)) {
                            stream_report_fail();
                            break;
                        }
                        sock = stream_connect(&url, http_buf, sizeof(http_buf));
                        if (sock < 0) {
                            stream_report_fail();
                            break;
                        }

                        xStreamBufferReset(ipc->recv_buffer);
                        header_block_size = 0;

                        state = STREAM_PARSING_HEADER;
                        LOG_DEBUG("IDLE -> PARSING_HEADER");
                    }
                    break;

                case STREAM_PARSING_HEADER:
                case STREAM_RUNNING:
                    if (msg.type == IPC_MSG_STREAM_STOP) {
                        stream_disconnect(&sock);
                        stream_report_stopped();
                        state = STREAM_IDLE;
                        LOG_DEBUG("RUNNING -> IDLE");
                    }
                    break;

                default:
                    break;
            }
        }

        /* Streaming logic */
        switch (state) {
            case STREAM_PARSING_HEADER: {
                if (header_block_size >= sizeof(http_buf)) {
                    LOG_ERROR("No header found or header too big!");
                    stream_disconnect(&sock);
                    stream_report_fail();
                    state = STREAM_IDLE;
                    LOG_DEBUG("PARSING_HEADER -> IDLE");
                    break;
                }

                const int len = recv(sock, &http_buf[header_block_size], sizeof(http_buf) - header_block_size, 0);
                if (len <= 0) {
                    LOG_ERROR("Connection closed unexpectedly");
                    stream_disconnect(&sock);
                    stream_report_fail();
                    state = STREAM_IDLE;
                    LOG_DEBUG("PARSING_HEADER -> IDLE");
                    break;
                }
                header_block_size += len;

                uint8_t *body_start = stream_find_body_start(http_buf, header_block_size);
                if (body_start == NULL) {
                    break; // Wait for more data
                }

                body_start[-1] = '\0'; // Null-terminate at last header byte
                if (!stream_header_check(http_buf)) {
                    stream_disconnect(&sock);
                    stream_report_fail();
                    state = STREAM_IDLE;
                    LOG_DEBUG("PARSING_HEADER -> IDLE");
                    break;
                }

                const size_t header_size = body_start - http_buf;
                const size_t body_size = header_block_size - header_size;
                xStreamBufferSend(ipc->recv_buffer, body_start, body_size, STREAM_BUFFER_SEND_TIMEOUT_TICKS);

                stream_report_running();

                state = STREAM_RUNNING;
                LOG_DEBUG("PARSING_HEADER -> RUNNING");
            }
            break;

            case STREAM_RUNNING: {
                const int len = recv(sock, http_buf, sizeof(http_buf), 0);
                if (len <= 0) {
                    LOG_ERROR("Connection closed unexpectedly");
                    stream_disconnect(&sock);
                    stream_report_fail();
                    state = STREAM_IDLE;
                    LOG_DEBUG("RUNNING-> IDLE");
                    break;
                }
                xStreamBufferSend(ipc->recv_buffer, http_buf, len, STREAM_BUFFER_SEND_TIMEOUT_TICKS);
            }
            break;
        }
    }
}

void stream_init(void)
{
    xTaskCreate(stream_task,
                STREAM_TASK_NAME,
                STREAM_TASK_STACK_SIZE,
                NULL,
                STREAM_TASK_PRIO,
                NULL);
}
