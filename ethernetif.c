#include <enc28j60.h>
#include <FreeRTOS.h>
#include <timers.h>
#include <lwip/netif.h>
#include <lwip/etharp.h>
#include <lwip/sys.h>
#include <string.h>

#define ETHERNETIF_WORKER_NAME "ethif_worker"
#define ETHERNETIF_WORKER_STACK_SIZE (1024 * 2)
#define ETHERNETIF_WORKER_PRIO 1

#define ETHERNETIF_LINK_CHECK_TIMER_NAME "link_check"
#define ETHERNETIF_LINK_CHECK_INTERVAL_MS 500

static const uint8_t mac_addr[] = {0xDE, 0xAD, 0xBA, 0xBE, 0xCA, 0xFE};

static void ethif_link_check_callback(TimerHandle_t t)
{
    static bool last_link_up;

    struct netif *netif = pvTimerGetTimerID(t);
    const bool current_link_up = enc28j60_is_link_up();

    if (current_link_up != last_link_up) {
        if (current_link_up) {
            printf("Link up\n");
            netif_set_link_up(netif);
        }
        else {
            printf("Link down\n");
            netif_set_link_down(netif);
        }

        last_link_up = current_link_up;
    }
}

static void ethif_worker(void *arg)
{
    struct netif *netif = arg;
    static uint8_t rx_buf[1520];

    printf("%s: started at core %d\n", __func__, portGET_CORE_ID());

    /* Create and start link check timer */
    TimerHandle_t link_timer = xTimerCreate(ETHERNETIF_LINK_CHECK_TIMER_NAME, 
                                            pdMS_TO_TICKS(ETHERNETIF_LINK_CHECK_INTERVAL_MS),
                                            pdTRUE, 
                                            netif, 
                                            ethif_link_check_callback);
    xTimerStart(link_timer, 0);

    int cnt = 0;

    while (1) {
        if (enc28j60_rx_packets_pending()) {
            const size_t len = enc28j60_receive_packet(rx_buf, sizeof(rx_buf));

            struct pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
            // TODO NULL check
            // TODO pbuf chain?

            memcpy(p->payload, rx_buf, p->len);

            if (netif->input(p, netif) != ERR_OK) {
                printf("input failed!\n");
                pbuf_free(p);
            }
        }
        else {
            sys_arch_msleep(10);

            ++cnt;
            if (cnt > 100) {
                printf("%s: free stack: %u\n", __func__, uxTaskGetStackHighWaterMark(NULL) * 4);
                cnt = 0;
            }
        }
    }
}

static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
    static uint8_t tx_buf[1520]; // TODO remove this, write directly to ENC's buffer
    size_t len = 0;

    for (struct pbuf *q = p; q != NULL; q = q->next) {
        memcpy(&tx_buf[len], q->payload, q->len);
        len += q->len;
    }

    enc28j60_send_packet(tx_buf, len);

    return ERR_OK;
}

static void low_level_init(struct netif *netif)
{
    enc28j60_init(mac_addr);

    netif->output = etharp_output;
    netif->linkoutput = low_level_output;
    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_ETHARP | NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHERNET;
    netif->hwaddr_len = ETH_HWADDR_LEN;
    memcpy(netif->hwaddr, mac_addr, ETH_HWADDR_LEN);
    memcpy(netif->name, "en", 2);

    sys_thread_new(ETHERNETIF_WORKER_NAME, ethif_worker, netif, ETHERNETIF_WORKER_STACK_SIZE, ETHERNETIF_WORKER_PRIO);
}

err_t ethernetif_init(struct netif *netif)
{
    low_level_init(netif);

    return ERR_OK;
}
