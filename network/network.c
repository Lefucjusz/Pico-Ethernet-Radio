#include "network.h"
#include <lwip/tcpip.h>
#include <lwip/dhcp.h>
#include <ethernetif.h>
#include <ipc_context.h>
#include <ipc_message.h>
#include <logger.h>

typedef struct
{
    struct netif netif;
    const ipc_ctx_t *ipc;
} network_ctx_t;

static network_ctx_t ctx;

static void netif_status_callback(struct netif *netif)
{
    LOG_INFO("Got IP: %s", ip4addr_ntoa(netif_ip4_addr(netif)));

    ipc_manager_msg_t msg = {.type = IPC_MSG_NETWORK_GOT_IP};
    xQueueSend(ctx.ipc->manager_q, &msg, 0);
}

static void netif_link_callback(struct netif *netif)
{
    ipc_manager_msg_t msg;
    msg.type = netif_is_link_up(netif) ? IPC_MSG_NETWORK_LINK_UP : IPC_MSG_NETWORK_LINK_DOWN;
    xQueueSend(ctx.ipc->manager_q, &msg, 0);
}

void network_init(void)
{
    ctx.ipc = ipc_context_get();

    tcpip_init(NULL, NULL);

    netif_add(&ctx.netif, NULL, NULL, NULL, NULL, ethernetif_init, tcpip_input);
    netif_set_default(&ctx.netif);
    netif_set_up(&ctx.netif);
    netif_set_status_callback(&ctx.netif, netif_status_callback);
    netif_set_link_callback(&ctx.netif, netif_link_callback);

    dhcp_start(&ctx.netif);
}
