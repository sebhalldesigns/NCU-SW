#include "lwip/netif.h"
#include "ethernetif.h"

/* Stub - full implementation comes when we set up the ETH peripheral */
err_t ethernetif_init(struct netif *netif)
{
    return ERR_OK;
}

void ethernetif_input(struct netif *netif)
{
    /* Will poll ETH DMA RX descriptors */
}