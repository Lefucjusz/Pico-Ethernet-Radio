#include <enc28j60.h>
#include <enc28j60_defs.h>
#include <FreeRTOS.h>
#include <semphr.h>
#include <lwip/netif.h>
#include <lwip/etharp.h>
#include <lwip/dhcp.h>
#include <logger.h>
#include <utils.h>
#include <pico/unique_id.h>
#include <pico/rand.h>

#define ETHERNETIF_WORKER_NAME "ethif_worker"
#define ETHERNETIF_WORKER_STACK_SIZE UTILS_STACK_BYTES_TO_WORDS(1024 * 1)
#define ETHERNETIF_WORKER_PRIO 1

typedef struct
{
    SemaphoreHandle_t irq_sem;
    uint8_t mac_addr[ETH_HWADDR_LEN];
} ethif_ctx_t;

static ethif_ctx_t ctx;

static void ethif_get_mac(uint8_t *mac, bool random)
{
    if (random) {
        const uint64_t r = get_rand_64();
        memcpy(mac, &r, ETH_HWADDR_LEN);
    }
    else {
        pico_unique_board_id_t id;
        pico_get_unique_board_id(&id);
        memcpy(mac, id.id, ETH_HWADDR_LEN);
    }

    /* Clear multicast bit and set locally administered bit */
    mac[0] &= ~(1 << 0);
    mac[0] |= (1 << 1);
}

static void ethif_irq_callback(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(ctx.irq_sem, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void ethif_check_link(struct netif *netif, bool link_up)
{
    if (link_up) {
        netif_set_link_up(netif);
        dhcp_start(netif);
    }
    else {
        dhcp_stop(netif);
        netif_set_link_down(netif);
    }
}

static void ethif_receive_packets(struct netif *netif)
{
    static uint8_t rx_buf[1520];

    while (enc28j60_get_rx_packets_count() > 0) {
        const size_t len = enc28j60_receive_packet(rx_buf, sizeof(rx_buf));

        struct pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
        configASSERT(p != NULL);

        size_t offset = 0;
        for (struct pbuf *q = p; q != NULL; q = q->next) {
            memcpy(q->payload, &rx_buf[offset], q->len);
            offset += q->len;
        }

        if (netif->input(p, netif) != ERR_OK) {
            LOG_ERROR("netif input failed!");
            pbuf_free(p);
        }
    }
}

static void ethif_worker(void *arg)
{
    struct netif *netif = arg;

    LOG_INFO("Started at core %d", portGET_CORE_ID());

    ethif_check_link(netif, enc28j60_is_link_up());

    while (1) {
        xSemaphoreTake(ctx.irq_sem, portMAX_DELAY);

        enc28j60_enable_interrupts(false);

        const uint8_t flags = enc28j60_get_irq_flags();

        if ((flags & ENC28J60_EIR_LINKIF) != 0) {
            ethif_check_link(netif, enc28j60_is_link_up());
        }

        /* Errata Rev. B7, Issue 4 - PKTIF flag is unreliable, always check EPKTCNT */
        ethif_receive_packets(netif);

        enc28j60_enable_interrupts(true);
    }
}

static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
    static_assert(LWIP_NETIF_TX_SINGLE_PBUF > 0, "Single PBUF required for zero-copy implementation to work");

    enc28j60_send_packet(p->payload, p->len);

    return ERR_OK;
}

static void low_level_init(struct netif *netif)
{
    ctx.irq_sem = xSemaphoreCreateBinary();

    ethif_get_mac(ctx.mac_addr, false);
    LOG_INFO("MAC: %02x:%02x:%02x:%02x:%02x:%02x", ctx.mac_addr[0], ctx.mac_addr[1], ctx.mac_addr[2], ctx.mac_addr[3], ctx.mac_addr[4], ctx.mac_addr[5]);

    enc28j60_init(ctx.mac_addr);
    enc28j60_set_irq_callback(ethif_irq_callback);

    netif->output = etharp_output;
    netif->linkoutput = low_level_output;
    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_ETHARP | NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHERNET;
    netif->hwaddr_len = ETH_HWADDR_LEN;
    memcpy(netif->hwaddr, ctx.mac_addr, ETH_HWADDR_LEN);
    memcpy(netif->name, "en", 2);

    xTaskCreate(ethif_worker,
                ETHERNETIF_WORKER_NAME,
                ETHERNETIF_WORKER_STACK_SIZE,
                netif,
                ETHERNETIF_WORKER_PRIO,
                NULL);
}

err_t ethernetif_init(struct netif *netif)
{
    low_level_init(netif);

    return ERR_OK;
}
