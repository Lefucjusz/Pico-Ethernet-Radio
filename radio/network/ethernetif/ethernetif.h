#pragma once

#include <lwip/err.h>
#include <lwip/netif.h>

err_t ethernetif_init(struct netif *netif);
