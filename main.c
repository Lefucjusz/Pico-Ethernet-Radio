#include <pico/stdlib.h>
#include <FreeRTOS.h>
#include <task.h>
#include <stream_buffer.h>
#include <stdio.h>
#include <lwip/tcpip.h>
#include <lwip/dhcp.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <lwip/netdb.h>
#include <hardware/clocks.h>

StreamBufferHandle_t network_buffer;

#define LED_PIN 25

extern err_t ethernetif_init(struct netif *netif); // TODO
extern void player_start(void);

#define HOST "mp3.polskieradio.pl"
#define PORT 8904

static int fetch_webpage(void)
{
    int sock;
    struct sockaddr_in server_addr;
    struct hostent *host;
    static uint8_t buffer[1024];

    /* Resolve hostname */
    host = gethostbyname(HOST);
    if (host == NULL) {
        printf("DNS lookup failed\n");
        return -1;
    }

    /* Create socket */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("Socket creation failed\n");
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    memcpy(&server_addr.sin_addr,
           host->h_addr_list[0],
           host->h_length);

    /* Connect */
    if (connect(sock,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0) {
        printf("Connect failed\n");
        closesocket(sock);
        return -1;
    }

    /* Send HTTP request */
    const char *request =
        "GET / HTTP/1.0\r\n"
        "Host: " HOST "\r\n"
        "User-Agent: lwip-radio\r\n"
        "Icy-MetaData: 0\r\n"
        "\r\n";

    send(sock, request, strlen(request), 0);

    printf("%s: request sent\n", __func__);

    int len;
    int bytes_recvd = 0;
    while (bytes_recvd < sizeof(buffer) - 1) {
        len = recv(sock, &buffer[bytes_recvd], sizeof(buffer) - 1 - bytes_recvd, 0);
        bytes_recvd += len;
    }
    buffer[bytes_recvd] = '\0';

    /* Strip header */
    char *stream_start = strstr(buffer, "\r\n\r\n");
    if (stream_start == NULL) {
        printf("No header end found!\n");
    }
    else {
        printf("Header: %.*s\n", stream_start - (char*)buffer - 1, buffer);
    }
    stream_start += 4;

    xStreamBufferSend(network_buffer, stream_start, (char*)buffer + sizeof(buffer) - 1 - stream_start, portMAX_DELAY);

    
    printf("%s: free stack: %u\n", __func__, uxTaskGetStackHighWaterMark(NULL) * 4);

    /* Receive response */
    while ((len = recv(sock, buffer, sizeof(buffer), 0)) > 0) {
        xStreamBufferSend(network_buffer, buffer, len, portMAX_DELAY);
    }

    closesocket(sock);
    return 0;
}

static void netif_status_callback(struct netif *netif)
{
    printf("IP changed: %s\n", ip4addr_ntoa(netif_ip4_addr(netif)));
}

static void bootstrap_task(void *arg)
{
    static struct netif netif;

    printf("%s: started at core %d\n", __func__, portGET_CORE_ID());

    network_buffer = xStreamBufferCreate(32768, 0);

    tcpip_init(NULL, NULL);

    netif_add(&netif, NULL, NULL, NULL, NULL, ethernetif_init, tcpip_input);
    netif_set_default(&netif);
    netif_set_up(&netif);
    netif_set_status_callback(&netif, netif_status_callback);

    dhcp_start(&netif);

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    vTaskDelay(5000);

    player_start();
    fetch_webpage();

    while (1) {
        gpio_xor_mask(1 << LED_PIN);
        vTaskDelay(1000);
    }
}

int main(void)
{
    set_sys_clock_khz(150000, true);

    stdio_init_all();

    xTaskCreateAffinitySet(bootstrap_task, "bootstrap", 2048 / sizeof(uint32_t), NULL, 1, 1 << 0, NULL);

    vTaskStartScheduler();

    /* Unreachable */
    while (1) {}
}
