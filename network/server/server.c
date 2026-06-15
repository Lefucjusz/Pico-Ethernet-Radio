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
#define SERVER_TASK_STACK_SIZE UTILS_STACK_BYTES_TO_WORDS(1024 * 1.5)
#define SERVER_TASK_PRIO 1

#define SERVER_STATUS_RESPONSE_TIMEOUT_TICKS pdMS_TO_TICKS(500)
#define SERVER_SEND_CHUNK_SIZE_BYTES 1024

typedef enum
{
    SERVER_HTTP_OK = 200,
    SERVER_HTTP_NO_CONTENT = 204,
    SERVER_HTTP_BAD_REQUEST = 400,
    SERVER_HTTP_NOT_FOUND = 404,
    SERVER_HTTP_INTERNAL_ERROR = 500
} server_http_status_t;

typedef struct
{
    const char *method;
    size_t method_len;
    const char *path;
    size_t path_len;
    const char *query;
    size_t query_len;
} server_http_req_t;

typedef struct
{
    const char *method;
    const char *path;
    void (*handler)(int client, const server_http_req_t *req);
} server_route_t;

typedef struct
{
    char http_buf[1024];
    const ipc_ctx_t *ipc;
} server_ctx_t;

static void server_handle_root(int client, const server_http_req_t *req);
static void server_handle_volume(int client, const server_http_req_t *req);
static void server_handle_start(int client, const server_http_req_t *req);
static void server_handle_stop(int client, const server_http_req_t *req);
static void server_handle_status(int client, const server_http_req_t *req);
static void server_handle_reboot(int client, const server_http_req_t *req);

/* Route table */
static const server_route_t routes[] = {
    {"GET", "/", server_handle_root},
    {"GET", "/volume", server_handle_volume},
    {"GET", "/start", server_handle_start},
    {"GET", "/stop", server_handle_stop},
    {"GET", "/status", server_handle_status},
    {"GET", "/reboot", server_handle_reboot}
};

static server_ctx_t ctx;

static const char *server_status_str(server_http_status_t status)
{
    switch (status) {
        case SERVER_HTTP_OK:
            return "OK";

        case SERVER_HTTP_NO_CONTENT:
            return "No Content";

        case SERVER_HTTP_BAD_REQUEST:
            return "Bad Request";

        case SERVER_HTTP_NOT_FOUND:
            return "Not Found";

        case SERVER_HTTP_INTERNAL_ERROR:
            return "Internal Server Error";

        default:
            return "Unknown";
    }
}

static void server_send(int client, const char *data, size_t size)
{
    size_t total_sent = 0;
    while (total_sent < size) {
        const size_t bytes_to_send = UTILS_MIN(size - total_sent, SERVER_SEND_CHUNK_SIZE_BYTES);
        const size_t sent = send(client, &data[total_sent], bytes_to_send, 0);
        total_sent += sent;
    }
}

static void server_send_response(int client, int status_code, const char *content_type, const char *body)
{
    if (status_code == SERVER_HTTP_NO_CONTENT) {
        const int header_len = snprintf(
            ctx.http_buf,
            sizeof(ctx.http_buf),
            "HTTP/1.1 %d %s\r\n"
            "Connection: close\r\n"
            "\r\n",
            status_code,
            server_status_str(status_code)
        );
        server_send(client, ctx.http_buf, header_len);
    }
    else {
        const size_t body_len = strlen(body);

        /* Prepare and send header */
        const int header_len = snprintf(
            ctx.http_buf,
            sizeof(ctx.http_buf),
            "HTTP/1.1 %d %s\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %u\r\n"
            "Connection: close\r\n"
            "\r\n",
            status_code,
            server_status_str(status_code),
            content_type,
            body_len
        );
        server_send(client, ctx.http_buf, header_len);

        /* Send body */
        server_send(client, body, body_len);
    }
}

static bool server_query_get(const server_http_req_t *req, const char *key, const char **out, size_t *len)
{
    const size_t key_len = strlen(key);

    if (req->query_len < (key_len + 2)) { // +2 -> '=' and at least one value char
        return false;
    }

    if ((memcmp(req->query, key, key_len) != 0) || (req->query[key_len] != '=')) {
        return false;
    }

    *out = req->query + key_len + 1;
    *len = req->query_len - (key_len + 1);

    return true;
}

static bool server_parse_int(const server_http_req_t *req, const char *key, int *out)
{
    const char *value;
    size_t value_len;
    if (!server_query_get(req, key, &value, &value_len)) {
        return false;
    }

    *out = atoi(value);

    return true;
}

static bool server_parse_str(const server_http_req_t *req, const char *key, char *out, size_t out_size)
{
    const char *value;
    size_t value_len;
    if (!server_query_get(req, key, &value, &value_len)) {
        return false;
    }

    if (value_len >= out_size) {
        return false;
    }

    memcpy(out, value, value_len);
    out[value_len] = '\0';

    return true;
}

static void server_handle_root(int client, const server_http_req_t *req)
{
    server_send_response(client, SERVER_HTTP_OK, "text/html", webpage);
}

static void server_handle_volume(int client, const server_http_req_t *req)
{
    int volume;

    if (server_parse_int(req, "value", &volume)) {
        ipc_manager_msg_t msg = {
            .type = IPC_MSG_UI_SET_VOLUME,
            .arg = (void *)volume
        };
        xQueueSend(ctx.ipc->manager_q, &msg, 0);

        server_send_response(client, SERVER_HTTP_NO_CONTENT, NULL, NULL);
    }
    else {
        server_send_response(client, SERVER_HTTP_BAD_REQUEST, "text/html", "<h1>400 Bad Request</h1>");
    }
}

static void server_handle_start(int client, const server_http_req_t *req)
{
    static char url[128];

    if (server_parse_str(req, "url", url, sizeof(url))) {
        ipc_manager_msg_t msg = {
            .type = IPC_MSG_UI_START_PLAYBACK,
            .arg = (void *)url
        };
        xQueueSend(ctx.ipc->manager_q, &msg, 0);

        server_send_response(client, SERVER_HTTP_NO_CONTENT, NULL, NULL);
    }
    else {
        server_send_response(client, SERVER_HTTP_BAD_REQUEST, "text/html", "<h1>400 Bad Request</h1>");
    }
}

static void server_handle_stop(int client, const server_http_req_t *req)
{
    ipc_manager_msg_t msg = {
        .type = IPC_MSG_UI_STOP_PLAYBACK,
    };
    xQueueSend(ctx.ipc->manager_q, &msg, 0);

    server_send_response(client, SERVER_HTTP_NO_CONTENT, NULL, NULL);
}

static void server_handle_status(int client, const server_http_req_t *req)
{
    ipc_server_msg_t resp;
    char status_buf[256];

    /* Request current status */
    ipc_manager_msg_t msg = {
        .type = IPC_MSG_UI_GET_STATUS
    };
    xQueueSend(ctx.ipc->manager_q, &msg, 0);

    /* Receive response */
    if (xQueueReceive(ctx.ipc->server_q, &resp, SERVER_STATUS_RESPONSE_TIMEOUT_TICKS) == pdTRUE) {
        snprintf(status_buf, sizeof(status_buf),
            "{"
            "\"state\":%d,"
            "\"volume\":%u,"
            "\"url\":\"%s\""
            "}",
            resp.status.state,
            resp.status.volume,
            resp.status.stream_url
        );

        server_send_response(client, SERVER_HTTP_OK, "application/json", status_buf);
    }
    else {
        server_send_response(client, SERVER_HTTP_INTERNAL_ERROR, "text/html", "<h1>500 Internal Server Error</h1>");
    }
}

static void server_handle_reboot(int client, const server_http_req_t *req)
{
    ipc_manager_msg_t msg = {
        .type = IPC_MSG_UI_REQUEST_REBOOT
    };
    xQueueSend(ctx.ipc->manager_q, &msg, 0);

    server_send_response(client, SERVER_HTTP_NO_CONTENT, NULL, NULL);
}

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
        close(sock);
        return -1;
    }

    if (listen(sock, 2) < 0) {
        close(sock);
        return -1;
    }

    return sock;
}

static bool server_path_equal(const server_http_req_t *req, const char *path)
{
    const size_t path_len = strlen(path);
    return ((path_len == req->path_len) && (memcmp(req->path, path, path_len) == 0));
}

static bool server_method_equal(const server_http_req_t *req, const char *method)
{
    const size_t method_len = strlen(method);
    return ((method_len == req->method_len) && (memcmp(req->method, method, method_len) == 0));
}

static void server_handle_request(int client, const server_http_req_t *req)
{
    for (size_t i = 0; i < UTILS_ARRAY_COUNT(routes); ++i) {
        if (server_method_equal(req, routes[i].method) && server_path_equal(req, routes[i].path)) {
            routes[i].handler(client, req);
            return;
        }
    }

    server_send_response(client, SERVER_HTTP_NOT_FOUND, "text/html", "<h1>404 Not Found</h1>");
}

static int server_receive_request(int client, char *buffer, size_t size)
{
    int total = 0;

    while (total < (size - 1)) {
        const int len = recv(client, &buffer[total], size - 1 - total, 0);
        if (len < 0) {
            return -1;
        }
        if (len == 0) {
            break;
        }

        total += len;
        buffer[total] = '\0';

        if (strstr(buffer, "\r\n\r\n") != NULL) {
            return total;
        }
    }

    return (total == 0) ? 0 : -1;
}

static bool server_parse_request(const char *buffer, server_http_req_t *req)
{
    /* Extract method */
    const char *method_end = strchr(buffer, ' ');
    if (method_end == NULL) {
        return false;
    }
    req->method = buffer;
    req->method_len = method_end - buffer;

    const char *path_start = method_end + 1;
    const char *path_end = strchr(path_start, ' ');
    if (path_end == NULL) {
        return false;
    }

    /* Check if question mark in path and extract path/query */
    const char *qmark = memchr(path_start, '?', path_end - path_start);
    if (qmark != NULL) {
        req->path = path_start;
        req->path_len = qmark - path_start;

        req->query = qmark + 1;
        req->query_len = path_end - (qmark + 1);
    }
    else {
        req->path = path_start;
        req->path_len = path_end - path_start;

        req->query = NULL;
        req->query_len = 0;
    }

    return true;
}

static void server_task(void *arg)
{
    struct sockaddr_in addr;
    server_http_req_t request;

    ctx.ipc = ipc_context_get();

    LOG_INFO("Started at core %d", portGET_CORE_ID());

    int sock = server_create();
    if (sock < 0) {
        LOG_FATAL("Failed to create HTTP server!");
        configASSERT(0);
    }
    
    while (1) {
        socklen_t addr_len = sizeof(addr);

        int client = accept(sock, (struct sockaddr *)&addr, &addr_len);
        if (client >= 0) {
            const int len = server_receive_request(client, ctx.http_buf, sizeof(ctx.http_buf));
            if (len > 0) {
                if (server_parse_request(ctx.http_buf, &request)) {
                    server_handle_request(client, &request);
                }
                else {
                    LOG_ERROR("Failed to parse request");
                }
            }
            else {
                LOG_ERROR("Failed to receive request");
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
