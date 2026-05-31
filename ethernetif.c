#include <enc28j60.h>
#include <lwip/netif.h>
#include <lwip/etharp.h>
#include <lwip/sys.h>
#include <string.h>

#define ETHERNETIF_WORKER_THREAD_NAME "ethif_worker"
#define ETHERNETIF_WORKER_THREAD_STACK_SIZE (1024 * 4)
#define ETHERNETIF_WORKER_THREAD_PRIO 1

#define ETHERNETIF_LINK_STATUS_CHECK_INTERVAL_MS 500

static const uint8_t mac_addr[] = {0xDE, 0xAD, 0xBA, 0xBE, 0xCA, 0xFE};

static void ethif_worker(void *arg)
{
    struct netif *netif = arg;

    static uint8_t rx_buf[1520];

    bool link_up = false;
    uint32_t link_check_tick = 0;

    printf("%s: start\n", __func__);

    while (1) {
        /* Link state check */
        const uint32_t current_tick = sys_now();
        if ((current_tick - link_check_tick) >= ETHERNETIF_LINK_STATUS_CHECK_INTERVAL_MS) {
            const bool current_link_up = enc28j60_is_link_up();
            if (link_up != current_link_up) {
                if (current_link_up) {
                    printf("Link up\n");
                    netif_set_link_up(netif);
                }
                else {
                    printf("Link down\n");
                    netif_set_link_down(netif);
                }

                link_up = current_link_up;
            }

            link_check_tick = current_tick;
        }

        /* Packet Rx */
        if (enc28j60_rx_packets_pending()) {
            const size_t len = enc28j60_receive_packet(rx_buf, sizeof(rx_buf));

            printf("Rx len=%u\n", len);

            struct pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
            // TODO NULL check

            memcpy(p->payload, rx_buf, p->len);

            if (netif->input(p, netif) != ERR_OK) {
                printf("input failed!\n");
                pbuf_free(p);
            }
        }
        else {
            sys_arch_msleep(10);
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

    printf("Tx len=%d\n", len);

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

    sys_thread_new(ETHERNETIF_WORKER_THREAD_NAME, ethif_worker, netif, ETHERNETIF_WORKER_THREAD_STACK_SIZE, ETHERNETIF_WORKER_THREAD_PRIO);
}

err_t ethernetif_init(struct netif *netif)
{
    low_level_init(netif);

    return ERR_OK;
}
