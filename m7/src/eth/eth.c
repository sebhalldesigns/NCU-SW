/***************************************************************
**
** NCU Source File
**
** File         :  eth.c
** Module       :  eth
** Author       :  SH
** Created      :  2026-03-28 (YYYY-MM-DD)
** License      :  MIT
** Description  :  NCU Ethernet Interface
**
***************************************************************/

/***************************************************************
** MARK: INCLUDES
***************************************************************/

#include "eth.h"

#include <lwip/init.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <lwip/etharp.h>
#include <lwip/timeouts.h>
#include <lwip/dhcp.h>
#include <lwip/err.h>
#include <netif/ethernet.h>

#include "stm32h745xx.h"


/***************************************************************
** MARK: CONSTANTS & MACROS
***************************************************************/

#define ETH_RX_DESC_COUNT   4
#define ETH_TX_DESC_COUNT   4
#define ETH_BUFFER_SIZE     1524
#define ETH_PHY_ADDRESS     0U
#define ETH_PHY_REG_BSR     1U
#define ETH_PHY_REG_SCSR    31U
#define ETH_PHY_BSR_LINK    (1U << 2)
#define ETH_PHY_TIMEOUT     1000000U
#define ETH_TX_CIC_FULL     (0x3U << 16)

/***************************************************************
** MARK: TYPEDEFS
***************************************************************/

/* ===================== DMA DESCRIPTORS ===================== */
/* Must live in D2 SRAM for ETH DMA access */

/* DMA descriptor - H745 uses enhanced descriptors */
typedef struct {
    volatile uint32_t DESC0;
    volatile uint32_t DESC1;
    volatile uint32_t DESC2;
    volatile uint32_t DESC3;
} ETH_DMADescTypeDef;


/***************************************************************
** MARK: STATIC VARIABLES
***************************************************************/

/* Place everything in D2 SRAM */
__attribute__((section(".eth_dma"))) __attribute__((aligned(32)))
static ETH_DMADescTypeDef RxDescriptors[ETH_RX_DESC_COUNT];

__attribute__((section(".eth_dma"))) __attribute__((aligned(32)))
static ETH_DMADescTypeDef TxDescriptors[ETH_TX_DESC_COUNT];

__attribute__((section(".eth_dma"))) __attribute__((aligned(32)))
static uint8_t RxBuffers[ETH_RX_DESC_COUNT][ETH_BUFFER_SIZE];

__attribute__((section(".eth_dma"))) __attribute__((aligned(32)))
static uint8_t TxBuffers[ETH_TX_DESC_COUNT][ETH_BUFFER_SIZE];

/* Track descriptor indices */
static uint32_t RxDescIdx = 0;
static uint32_t TxDescIdx = 0;
static bool LinkUp = false;


static struct netif eth_netif;

/***************************************************************
** MARK: STATIC FUNCTION DEFS
***************************************************************/

static err_t ethernetif_init(struct netif *netif);
static err_t ethernetif_output(struct netif *netif, struct pbuf *p);
static void ethernetif_input(struct netif *netif);
static bool ethernetif_read_phy(uint32_t reg, uint32_t *value);
static void ethernetif_apply_link_mode();
static void ethernetif_update_link_state();

/***************************************************************
** MARK: GLOBAL FUNCTIONS
***************************************************************/

bool eth_init()
{
    /* Enable ETH clocks */
    RCC->AHB1ENR |= RCC_AHB1ENR_ETH1MACEN;
    RCC->AHB1ENR |= RCC_AHB1ENR_ETH1TXEN;
    RCC->AHB1ENR |= RCC_AHB1ENR_ETH1RXEN;

    lwip_init();

    /* IP config - use zero for DHCP */
    ip4_addr_t ipaddr, netmask, gw;
    ip4_addr_set_zero(&ipaddr);
    ip4_addr_set_zero(&netmask);
    ip4_addr_set_zero(&gw);

    /* Add network interface */
    netif_add(&eth_netif, &ipaddr, &netmask, &gw, NULL, ethernetif_init, ethernet_input);

    /* Set as default interface and bring up */
    netif_set_default(&eth_netif);
    netif_set_up(&eth_netif);

    /* Link state drives DHCP state machine in NO_SYS mode. */
    ethernetif_update_link_state();

    return true;
}

void eth_poll()
{
    /* ===================== DIAGNOSTICS ===================== */

    /* MAC & DMA Status */
    volatile uint32_t dma_sr = ETH->DMACSR;         /* DMA Channel Status Register */
    volatile uint32_t mac_rxtx_sr = ETH->MACRXTXSR; /* MAC RX/TX Status Register */
    volatile uint32_t mac_pcs_sr = ETH->MACPCSR;    /* MAC PCS Control and Status Register */
    volatile uint32_t mac_cr = ETH->MACCR;          /* MAC Control Register */

    /* Check RX descriptor state */
    volatile uint32_t desc3_current = RxDescriptors[RxDescIdx].DESC3;
    volatile uint32_t desc_own_bit = (desc3_current >> 31) & 1;  /* Bit 31: OWN */
    volatile uint32_t desc_length = desc3_current & 0x7FFF;       /* Frame length */

    /* DMA RPS state (bits 17:15) - 0=stopped, 1=fetching, 3=waiting, 6=suspended */
    volatile uint32_t dma_rps = (dma_sr >> 15) & 0x7;

    /* DMA TPS state (bits 12:10) */
    volatile uint32_t dma_tps = (dma_sr >> 10) & 0x7;

    /* DMA Error bits */
    volatile uint32_t dma_fbes = (dma_sr >> 13) & 1;  /* Fatal Bus Error (bit 13) */
    volatile uint32_t dma_nis = (dma_sr >> 16) & 1;   /* Normal Interrupt (bit 16) */
    volatile uint32_t dma_ais = (dma_sr >> 14) & 1;   /* Abnormal Interrupt (bit 14) */
    volatile uint32_t dma_bit6 = (dma_sr >> 6) & 1;   /* Mystery bit 6 */

    /* Check if DMA RX/TX are actually enabled */
    volatile uint32_t dmac_tcr = ETH->DMACTCR;  /* TX control - bit 0 = ST */
    volatile uint32_t dmac_rcr = ETH->DMACRCR;  /* RX control - bit 0 = SR */
    volatile uint32_t dma_tx_enabled = (dmac_tcr >> 0) & 1;
    volatile uint32_t dma_rx_enabled = (dmac_rcr >> 0) & 1;

    /* Check MAC control bits */
    volatile uint32_t mac_te = (mac_cr >> 0) & 1;  /* MAC TX enable */
    volatile uint32_t mac_re = (mac_cr >> 1) & 1;  /* MAC RX enable */

    /* Check descriptor addresses are valid */
    volatile uint32_t dma_tx_desc_addr = ETH->DMACTDLAR;
    volatile uint32_t dma_rx_desc_addr = ETH->DMACRDLAR;
    volatile uint32_t dma_tx_ring_len = ETH->DMACTDRLR;
    volatile uint32_t dma_rx_ring_len = ETH->DMACRDRLR;

    /* MAC link status - check MACRXTXSR bit 0 (RXSTS) and bit 1 (TXSTS) */
    volatile uint32_t mac_rx_active = (mac_rxtx_sr >> 0) & 1;
    volatile uint32_t mac_tx_active = (mac_rxtx_sr >> 1) & 1;

    ethernetif_update_link_state();
    volatile uint32_t phy_link = LinkUp;

    /* Process incoming frames */
    ethernetif_input(&eth_netif);
    sys_check_timeouts();

    /* DHCP state tracking */
    struct dhcp *dhcp = netif_dhcp_data(&eth_netif);
    volatile uint8_t dhcp_state = (dhcp != NULL) ? dhcp->state : 0xFF;
    volatile ip4_addr_t current_ip = eth_netif.ip_addr;

    /* System time for tracking */
    volatile uint32_t ticks = sys_now();

}

/***************************************************************
** MARK: STATIC FUNCTIONS
***************************************************************/

static err_t ethernetif_init(struct netif *netif)
{
    netif->hwaddr_len = 6;
    netif->hwaddr[0]  = 0x00;
    netif->hwaddr[1]  = 0x12;
    netif->hwaddr[2]  = 0x34;
    netif->hwaddr[3]  = 0x56;
    netif->hwaddr[4]  = 0x78;
    netif->hwaddr[5]  = 0x9A;

    netif->mtu        = 1500;
    netif->flags      = NETIF_FLAG_BROADCAST
                      | NETIF_FLAG_ETHARP
                      | NETIF_FLAG_ETHERNET;

    netif->name[0]    = 'e';
    netif->name[1]    = 'n';

    netif->output     = etharp_output;
    netif->linkoutput = ethernetif_output;

    /* Software reset ETH DMA */
    ETH->DMAMR |= ETH_DMAMR_SWR;
    while (ETH->DMAMR & ETH_DMAMR_SWR);

    /* Keep MDC in-spec after reset (CR gets reset with SWR). */
    ETH->MACMDIOAR = (ETH->MACMDIOAR & ~ETH_MACMDIOAR_CR) | ETH_MACMDIOAR_CR_DIV124;

    /* DMA bus mode - address aligned beats, fixed burst */
    ETH->DMASBMR |= ETH_DMASBMR_AAL | ETH_DMASBMR_FB;

    /* MAC config: 100Mbps, full duplex, checksum offload */
    ETH->MACCR = ETH_MACCR_FES    /* 100Mbps */
               | ETH_MACCR_DM     /* full duplex */
               | ETH_MACCR_IPC;   /* checksum offload */

    /* Set hardware MAC from lwIP netif MAC to keep DHCP chaddr/L2 source consistent. */
    ETH->MACA0HR = ((uint32_t)netif->hwaddr[5] << 8) | (uint32_t)netif->hwaddr[4];
    ETH->MACA0LR = ((uint32_t)netif->hwaddr[3] << 24)
                 | ((uint32_t)netif->hwaddr[2] << 16)
                 | ((uint32_t)netif->hwaddr[1] << 8)
                 | ((uint32_t)netif->hwaddr[0]);

    /* MTL TX: store and forward mode */
    ETH->MTLTQOMR |= ETH_MTLTQOMR_TSF;

    /* MTL RX: store and forward mode */
    ETH->MTLRQOMR |= ETH_MTLRQOMR_RSF;

    /* Enable MAC TX and RX */
    ETH->MACCR |= ETH_MACCR_TE | ETH_MACCR_RE;


    /* ===================== RX DESCRIPTORS ===================== */
    for (int i = 0; i < ETH_RX_DESC_COUNT; i++)
    {
        RxDescriptors[i].DESC0 = (uint32_t)RxBuffers[i];
        RxDescriptors[i].DESC1 = 0;
        RxDescriptors[i].DESC2 = ETH_BUFFER_SIZE;  /* Buffer length - CRITICAL! */
        RxDescriptors[i].DESC3 = (1U << 31)   /* OWN: owned by DMA */
                               | (1U << 30)   /* IOC: interrupt on completion */
                               | (1U << 24);  /* BUF1V: buffer 1 valid */
    }

    /* ===================== TX DESCRIPTORS ===================== */
    for (int i = 0; i < ETH_TX_DESC_COUNT; i++)
    {
        TxDescriptors[i].DESC0 = 0;
        TxDescriptors[i].DESC1 = 0;
        TxDescriptors[i].DESC2 = 0;
        TxDescriptors[i].DESC3 = 0;
    }


    /* DMA channel descriptor skip length = 0 */
    ETH->DMACCR = ETH_DMACCR_DSL_0BIT;

    /* Point DMA at descriptor rings */
    ETH->DMACTDLAR  = (uint32_t)TxDescriptors;
    ETH->DMACTDRLR  = ETH_TX_DESC_COUNT - 1;
    ETH->DMACRDLAR  = (uint32_t)RxDescriptors;
    ETH->DMACRDRLR  = ETH_RX_DESC_COUNT - 1;

    /* RX buffer size in DMACRCR bits 13:1 */
    ETH->DMACRCR |= (ETH_BUFFER_SIZE << 1);

    /* Set tail pointers */
    ETH->DMACTDTPR = (uint32_t)&TxDescriptors[0];
    ETH->DMACRDTPR = (uint32_t)&RxDescriptors[ETH_RX_DESC_COUNT - 1];

    /* Start DMA TX and RX */
    ETH->DMACTCR |= (1U << 0);   /* ST: start TX */
    ETH->DMACRCR |= (1U << 0);   /* SR: start RX */

    return ERR_OK;

}

static err_t ethernetif_output(struct netif *netif, struct pbuf *p)
{
    ETH_DMADescTypeDef *desc = &TxDescriptors[TxDescIdx];

    /* Wait for descriptor to be free */
    while (desc->DESC3 & (1U << 31));

    /* Copy pbuf chain into TX buffer */
    uint8_t *buf = TxBuffers[TxDescIdx];
    uint16_t len = 0;
    for (struct pbuf *q = p; q != NULL; q = q->next)
    {
        memcpy(buf + len, q->payload, q->len);
        len += q->len;
    }

    /* Set up TX descriptor */
    desc->DESC0 = (uint32_t)buf;
    desc->DESC1 = 0;
    desc->DESC2 = len;              /* buffer length */
    desc->DESC3 = (1U << 31)        /* OWN: give to DMA */
                | (1U << 29)        /* FD: first descriptor */
                | (1U << 28)        /* LD: last descriptor */
                | ETH_TX_CIC_FULL   /* CIC: full IP + payload checksum insertion */
                | len;              /* frame length */

    /* Advance index */
    TxDescIdx = (TxDescIdx + 1) % ETH_TX_DESC_COUNT;

    /* Poll demand - wake up TX DMA */
    ETH->DMACTDTPR = (uint32_t)&TxDescriptors[TxDescIdx];

    return ERR_OK;
}

static void ethernetif_input(struct netif *netif)
{
    ETH_DMADescTypeDef *desc = &RxDescriptors[RxDescIdx];

    /* Check if DMA has written a frame - with frame count limit */
    int frame_count = 0;
    while (!(desc->DESC3 & (1U << 31)) && frame_count < 100)
    {
        frame_count++;
        uint16_t len = desc->DESC3 & 0x7FFF;  /* frame length */

        struct pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
        if (p != NULL)
        {
            memcpy(p->payload, RxBuffers[RxDescIdx], len);
            netif->input(p, netif);
        }

        /* Give descriptor back to DMA */
        desc->DESC0 = (uint32_t)RxBuffers[RxDescIdx];
        desc->DESC1 = 0;
        desc->DESC2 = ETH_BUFFER_SIZE;
        desc->DESC3 = (1U << 31) | (1U << 30) | (1U << 24);

        uint32_t returned_idx = RxDescIdx;

        /* Advance software index */
        RxDescIdx = (RxDescIdx + 1) % ETH_RX_DESC_COUNT;
        desc = &RxDescriptors[RxDescIdx];

        /* Tell DMA the last descriptor we just returned. */
        ETH->DMACRDTPR = (uint32_t)&RxDescriptors[returned_idx];
    }

    volatile uint32_t rx_frames = frame_count;
    (void)rx_frames;

}

static bool ethernetif_read_phy(uint32_t reg, uint32_t *value)
{
    if (value == NULL)
    {
        return false;
    }

    uint32_t timeout = ETH_PHY_TIMEOUT;
    while ((ETH->MACMDIOAR & ETH_MACMDIOAR_MB) != 0U)
    {
        if (timeout-- == 0U)
        {
            return false;
        }
    }

    uint32_t mdioar = ETH->MACMDIOAR;
    mdioar &= ~(ETH_MACMDIOAR_PA | ETH_MACMDIOAR_RDA | ETH_MACMDIOAR_MOC | ETH_MACMDIOAR_C45E);
    mdioar |= ((ETH_PHY_ADDRESS << ETH_MACMDIOAR_PA_Pos) & ETH_MACMDIOAR_PA)
           |  ((reg << ETH_MACMDIOAR_RDA_Pos) & ETH_MACMDIOAR_RDA)
           |  ETH_MACMDIOAR_MOC_RD
           |  ETH_MACMDIOAR_MB;
    ETH->MACMDIOAR = mdioar;

    timeout = ETH_PHY_TIMEOUT;
    while ((ETH->MACMDIOAR & ETH_MACMDIOAR_MB) != 0U)
    {
        if (timeout-- == 0U)
        {
            return false;
        }
    }

    *value = ETH->MACMDIODR & 0xFFFFU;
    return true;
}

static void ethernetif_apply_link_mode()
{
    uint32_t scsr = 0U;
    if (!ethernetif_read_phy(ETH_PHY_REG_SCSR, &scsr))
    {
        return;
    }

    uint32_t mode = (scsr >> 2) & 0x7U;
    uint32_t maccr = ETH->MACCR & ~(ETH_MACCR_FES | ETH_MACCR_DM);

    switch (mode)
    {
        case 0x1U: /* 10M half */
            break;
        case 0x5U: /* 10M full */
            maccr |= ETH_MACCR_DM;
            break;
        case 0x2U: /* 100M half */
            maccr |= ETH_MACCR_FES;
            break;
        case 0x6U: /* 100M full */
            maccr |= ETH_MACCR_FES | ETH_MACCR_DM;
            break;
        default:   /* unknown -> default 100M full */
            maccr |= ETH_MACCR_FES | ETH_MACCR_DM;
            break;
    }

    ETH->MACCR = maccr;
}

static void ethernetif_update_link_state()
{
    uint32_t bsr = 0U;
    bool link_now = false;

    /* BSR link bit is latch-low, so read twice. */
    if (ethernetif_read_phy(ETH_PHY_REG_BSR, &bsr) && ethernetif_read_phy(ETH_PHY_REG_BSR, &bsr))
    {
        link_now = (bsr & ETH_PHY_BSR_LINK) != 0U;
    }

    if (link_now == LinkUp)
    {
        return;
    }

    LinkUp = link_now;

    if (link_now)
    {
        ethernetif_apply_link_mode();
        netif_set_link_up(&eth_netif);
        dhcp_start(&eth_netif);
    }
    else
    {
        netif_set_link_down(&eth_netif);
        dhcp_stop(&eth_netif);
    }
}

