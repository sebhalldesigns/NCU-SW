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
#include <lwip/udp.h>
#include <lwip/tcp.h>
#include <netif/ethernet.h>
#include <lwip/apps/httpd.h>

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
#define ETH_TX_CIC_FULL     0U
#define ETH_TX_DESC_OWN     (1U << 31)
#define ETH_TX_DESC_FD      (1U << 29)
#define ETH_TX_DESC_LD      (1U << 28)
#define ETH_TX_DESC_B1L_MASK 0x3FFFU
#define ETH_TX_DESC_FL_MASK  0x7FFFU
#define ETH_TX_WAIT_SPINS   1000000U
#define ETH_LINK_CHECK_INTERVAL_MS 500U
#define ETH_LOG_LINE_MAX  220U
#define ETH_STDIO_RING_SIZE 1024U
#define ETH_TCP_RX_BUFFER_SIZE 1024U

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

static struct netif eth_netif;

/* Track PHY link state for change detection */
static uint32_t link_state_last = 0;
static uint32_t link_check_last_ms = 0U;

/* Flag to signal packet received (set by interrupt, cleared by main loop) */
volatile int eth_packet_ready = 0;

/* UDP support */
static struct udp_pcb *udp_pcb = NULL;
static eth_udp_recv_callback_t udp_recv_callback = NULL;

/* TCP server support */
static struct tcp_pcb *tcp_listener_pcb = NULL;
static struct tcp_pcb *tcp_client_pcb = NULL;
static eth_tcp_recv_callback_t tcp_recv_callback = NULL;
static uint16_t tcp_client_port = 0U;
static uint8_t tcp_rx_buffer[ETH_TCP_RX_BUFFER_SIZE];
static uint16_t tcp_rx_len = 0U;

/* Dedicated UDP logging stream state */
static struct udp_pcb *log_udp_pcb = NULL;
static ip_addr_t log_dst_addr;
static uint16_t log_dst_port = 0U;
static bool log_enabled = false;
static volatile uint16_t stdio_head = 0U;
static volatile uint16_t stdio_tail = 0U;
static uint8_t stdio_ring[ETH_STDIO_RING_SIZE];

/***************************************************************
** MARK: STATIC FUNCTION DEFS
***************************************************************/

static err_t ethernetif_init(struct netif *netif);
static err_t ethernetif_output(struct netif *netif, struct pbuf *p);
static void ethernetif_input(struct netif *netif);
static void ethernetif_service_dma();
static bool ethernetif_read_phy(uint32_t reg, uint32_t *value);
static void ethernetif_apply_link_mode();
static void ethernetif_update_link_state();
static eth_result_t eth_log_send(const uint8_t *data, uint16_t len);
static void eth_log_write_prefix(void);
static void eth_log_write_kv_sep(const char *label);
static void eth_log_puts(const char *s);
static void eth_log_put_u32(uint32_t value);
static void eth_log_put_i32(int32_t value);
static void eth_log_put_fixed3(uint32_t value);
static void eth_log_stdio_flush(void);
static void eth_udp_recv_handler(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                                  const ip_addr_t *addr, uint16_t port);
static void eth_log_ip4(const char *label, uint32_t addr);
static void eth_tcp_close_client(void);
static err_t eth_tcp_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err);
static err_t eth_tcp_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
static err_t eth_tcp_poll_cb(void *arg, struct tcp_pcb *tpcb);
static void eth_tcp_err_cb(void *arg, err_t err);

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

    IP4_ADDR(&ipaddr, 192, 168, 1, 50);
    IP4_ADDR(&netmask, 255, 255, 255, 0);
    IP4_ADDR(&gw, 192, 168, 1, 1);

    /* Add network interface */
    netif_add(&eth_netif, &ipaddr, &netmask, &gw, NULL, ethernetif_init, ethernet_input);

    /* Set as default interface and bring up */
    netif_set_default(&eth_netif);
    netif_set_up(&eth_netif);

    /* Link state drives DHCP state machine in NO_SYS mode. */
    ethernetif_update_link_state();

    /* Polling mode: keep ETH IRQ disabled to avoid interrupt storms. */
    NVIC_DisableIRQ(ETH_IRQn);
    NVIC_ClearPendingIRQ(ETH_IRQn);

    /* Initialize HTTP server */
    httpd_init();

    return true;
}

void eth_poll()
{
    uint32_t now = sys_now();
    if ((uint32_t)(now - link_check_last_ms) >= ETH_LINK_CHECK_INTERVAL_MS)
    {
        link_check_last_ms = now;
        ethernetif_update_link_state();
    }

    ethernetif_service_dma();

    /* Process incoming frames */
    ethernetif_input(&eth_netif);
    eth_log_stdio_flush();
    sys_check_timeouts();
}

eth_result_t eth_udp_bind(uint16_t port, eth_udp_recv_callback_t recv_callback)
{
    /* Create UDP PCB if not already created */
    if (udp_pcb == NULL)
    {
        udp_pcb = udp_new();
        if (udp_pcb == NULL)
        {
            return ETH_RES_ERR;
        }
    }

    /* Store callback */
    udp_recv_callback = recv_callback;

    /* Bind to port on any interface */
    err_t err = udp_bind(udp_pcb, IP_ADDR_ANY, port);
    if (err != ERR_OK)
    {
        return ETH_RES_ERR;
    }

    /* Register receive callback */
    udp_recv(udp_pcb, eth_udp_recv_handler, NULL);

    return ETH_RES_OK;
}

eth_result_t eth_udp_send(uint32_t dst_ip, uint16_t dst_port, const uint8_t *data, uint16_t len)
{
    if (udp_pcb == NULL)
    {
        return ETH_RES_ERR;
    }

    if (data == NULL || len == 0)
    {
        return ETH_RES_ERR;
    }

    /* Allocate pbuf */
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
    if (p == NULL)
    {
        return ETH_RES_ERR;
    }

    /* Copy data into pbuf */
    pbuf_take(p, data, len);

    /* Create destination address */
    ip_addr_t dst_addr;
    dst_addr.addr = dst_ip;

    /* Send the data */
    err_t err = udp_sendto(udp_pcb, p, &dst_addr, dst_port);

    /* Free pbuf (udp_sendto makes a copy) */
    pbuf_free(p);

    return (err == ERR_OK) ? ETH_RES_OK : ETH_RES_ERR;
}

eth_result_t eth_tcp_init(uint16_t port, eth_tcp_recv_callback_t recv_callback)
{
    tcp_recv_callback = recv_callback;

    if (tcp_listener_pcb != NULL)
    {
        return ETH_RES_OK;
    }

    tcp_listener_pcb = tcp_new();
    if (tcp_listener_pcb == NULL)
    {
        return ETH_RES_ERR;
    }

    if (tcp_bind(tcp_listener_pcb, IP_ADDR_ANY, port) != ERR_OK)
    {
        tcp_abort(tcp_listener_pcb);
        tcp_listener_pcb = NULL;
        return ETH_RES_ERR;
    }

    struct tcp_pcb *listen_pcb = tcp_listen_with_backlog(tcp_listener_pcb, 1U);
    if (listen_pcb == NULL)
    {
        tcp_abort(tcp_listener_pcb);
        tcp_listener_pcb = NULL;
        return ETH_RES_ERR;
    }

    tcp_listener_pcb = listen_pcb;
    tcp_arg(tcp_listener_pcb, NULL);
    tcp_accept(tcp_listener_pcb, eth_tcp_accept_cb);

    return ETH_RES_OK;
}

bool eth_tcp_is_connected(void)
{
    return tcp_client_pcb != NULL;
}

eth_result_t eth_tcp_send(const uint8_t *data, uint16_t len)
{
    if ((tcp_client_pcb == NULL) || (data == NULL) || (len == 0U))
    {
        return ETH_RES_ERR;
    }

    if (tcp_sndbuf(tcp_client_pcb) < len)
    {
        return ETH_RES_ERR;
    }

    if (tcp_write(tcp_client_pcb, data, len, TCP_WRITE_FLAG_COPY) != ERR_OK)
    {
        return ETH_RES_ERR;
    }

    if (tcp_output(tcp_client_pcb) != ERR_OK)
    {
        return ETH_RES_ERR;
    }

    return ETH_RES_OK;
}

bool eth_log_init(uint32_t dst_ip, uint16_t dst_port)
{
    if (log_udp_pcb != NULL)
    {
        udp_remove(log_udp_pcb);
        log_udp_pcb = NULL;
    }

    log_udp_pcb = udp_new();
    if (log_udp_pcb == NULL)
    {
        return false;
    }

    log_dst_addr.addr = dst_ip;
    log_dst_port = dst_port;
    log_enabled = true;
    return true;
}

void eth_log(const char *content)
{
    if ((!log_enabled) || (content == NULL))
    {
        return;
    }

    eth_log_write_prefix();
    eth_log_puts(content);
    eth_log_puts("\r\n");
}

void eth_log_u32(const char *label, uint32_t value)
{
    if (!log_enabled)
    {
        return;
    }

    eth_log_write_prefix();
    eth_log_write_kv_sep((label != NULL) ? label : "u32");
    eth_log_put_u32(value);
    eth_log_puts("\r\n");
}

void eth_log_i32(const char *label, int32_t value)
{
    if (!log_enabled)
    {
        return;
    }

    eth_log_write_prefix();
    eth_log_write_kv_sep((label != NULL) ? label : "i32");
    eth_log_put_i32(value);
    eth_log_puts("\r\n");
}

void eth_log_bool(const char *label, bool value)
{
    if (!log_enabled)
    {
        return;
    }

    eth_log_write_prefix();
    eth_log_write_kv_sep((label != NULL) ? label : "bool");
    eth_log_puts(value ? "true" : "false");
    eth_log_puts("\r\n");
}

void eth_log_putc_raw(uint8_t ch)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    uint16_t next = (uint16_t)((stdio_head + 1U) % ETH_STDIO_RING_SIZE);
    if (next == stdio_tail)
    {
        /* Drop when queue is full to keep ISR path non-blocking. */
    }
    else
    {
        stdio_ring[stdio_head] = ch;
        stdio_head = next;
    }

    __set_PRIMASK(primask);
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

    /* ===================== RMII GPIO CONFIG ===================== */
    /* All RMII pins: alternate function mode, AF11, high speed, no pull. */

    /* --- PA1 (RMII_REF_CLK), PA2 (RMII_MDIO), PA7 (RMII_CRS_DV) --- */
    /* Mode: AF (10) for pins 1, 2, 7 */
    GPIOA->MODER = (GPIOA->MODER
                & ~(0x3 << (1*2) | 0x3 << (2*2) | 0x3 << (7*2)))
                | (0x2 << (1*2) | 0x2 << (2*2) | 0x2 << (7*2));

    /* Speed: high (11) */
    GPIOA->OSPEEDR |= (0x3 << (1*2) | 0x3 << (2*2) | 0x3 << (7*2));

    /* No pull */
    GPIOA->PUPDR &= ~(0x3 << (1*2) | 0x3 << (2*2) | 0x3 << (7*2));

    /* AF11 for pins 1, 2 (AFRL) and pin 7 (AFRL) */
    GPIOA->AFR[0] = (GPIOA->AFR[0]
                    & ~(0xF << (1*4) | 0xF << (2*4) | 0xF << (7*4)))
                    | (11U << (1*4) | 11U << (2*4) | 11U << (7*4));

    /* --- PB1 (RMII_MDC), PB11 (RMII_TX_EN), PB12 (RMII_TXD0), PB13 (RMII_TXD1) --- */
    GPIOB->MODER = (GPIOB->MODER
                & ~(0x3 << (1*2) | 0x3 << (11*2) | 0x3 << (12*2) | 0x3 << (13*2)))
                | (0x2 << (1*2) | 0x2 << (11*2) | 0x2 << (12*2) | 0x2 << (13*2));

    GPIOB->OSPEEDR |= (0x3 << (1*2) | 0x3 << (11*2) | 0x3 << (12*2) | 0x3 << (13*2));
    GPIOB->PUPDR   &= ~(0x3 << (1*2) | 0x3 << (11*2) | 0x3 << (12*2) | 0x3 << (13*2));

    /* PB1 is in AFRL, PB11/12/13 are in AFRH. */
    GPIOB->AFR[0] = (GPIOB->AFR[0] & ~(0xF << (1*4))) | (11U << (1*4));
    GPIOB->AFR[1] = (GPIOB->AFR[1]
                    & ~(0xF << ((11-8)*4) | 0xF << ((12-8)*4) | 0xF << ((13-8)*4)))
                    | (11U << ((11-8)*4) | 11U << ((12-8)*4) | 11U << ((13-8)*4));

    /* --- PC4 (RMII_RXD0), PC5 (RMII_RXD1) --- */
    GPIOC->MODER = (GPIOC->MODER
                & ~(0x3 << (4*2) | 0x3 << (5*2)))
                | (0x2 << (4*2) | 0x2 << (5*2));

    GPIOC->OSPEEDR |= (0x3 << (4*2) | 0x3 << (5*2));
    GPIOC->PUPDR   &= ~(0x3 << (4*2) | 0x3 << (5*2));

    GPIOC->AFR[0] = (GPIOC->AFR[0]
                    & ~(0xF << (4*4) | 0xF << (5*4)))
                    | (11U << (4*4) | 11U << (5*4));

    /* --- PA15 (PHY reset) --- */
    GPIOA->MODER = (GPIOA->MODER & ~(0x3 << (15*2))) | (0x1 << (15*2));
    GPIOA->OTYPER &= ~(0x1 << 15);
    GPIOA->OSPEEDR |= (0x3 << (15*2));
    GPIOA->PUPDR &= ~(0x3 << (15*2));

    /* Hold PHY in reset briefly, then release it before any MDIO access. */
    GPIOA->BSRR = (1U << (15 + 16));
    for (volatile int i = 0; i < 100000; i++);
    GPIOA->BSRR = (1U << 15);
    for (volatile int i = 0; i < 100000; i++);

    /* Reset ETH MAC */
    RCC->AHB1RSTR |= RCC_AHB1RSTR_ETH1MACRST;
    for (volatile int i = 0; i < 1000; i++);
    RCC->AHB1RSTR &= ~RCC_AHB1RSTR_ETH1MACRST;

    /* Set MDIO clock divider - input clock is AHB (200MHz at our settings)
    CR = 100-168MHz range → use CR=4 (divide by 102)
    Actually at 200MHz use CR=5 (divide by 124) - bits 4:2 of MACMDIOAR */
    ETH->MACMDIOAR = (5U << 8);  /* CR field at bits 11:8 */

    /* ===================== MDIO READ FUNCTION ===================== */
    /* Read PHY register - PHY addr 0, register addr passed in */
    /* MDIO read: set PA=0, RDA=reg, GOC=11 (read), MB=1 */

    /* Read PHY ID register 2 (should return 0x0007) */
    ETH->MACMDIOAR = (ETH->MACMDIOAR & ~(0x1F << 21 | 0x1F << 16 | 0x3 << 2))
                | (0U  << 21)   /* PA: PHY address 0 */
                | (2U  << 16)   /* RDA: register 2 */
                | (0x3 << 2)    /* GOC: read operation */
                | (1U  << 0);   /* MB: busy, starts transfer */

    /* Wait for MDIO ready */
    while (ETH->MACMDIOAR & (1U << 0));

    volatile uint32_t phy_id2 = ETH->MACMDIODR & 0xFFFF;

    /* Read PHY ID register 3 (should return 0xC130) */
    ETH->MACMDIOAR = (ETH->MACMDIOAR & ~(0x1F << 21 | 0x1F << 16 | 0x3 << 2))
                | (0U  << 21)
                | (3U  << 16)
                | (0x3 << 2)
                | (1U  << 0);

    while (ETH->MACMDIOAR & (1U << 0));

    volatile uint32_t phy_id3 = ETH->MACMDIODR & 0xFFFF;

    /* ===================== PHY RESET ===================== */

    /* Write to PHY basic control register (reg 0) - set bit 15 (reset) */
    ETH->MACMDIODR = (1U << 15);  /* data to write */
    ETH->MACMDIOAR = (ETH->MACMDIOAR & ~(0x1F << 21 | 0x1F << 16 | 0x3 << 2))
                | (0U  << 21)   /* PA: PHY address 0 */
                | (0U  << 16)   /* RDA: register 0 - basic control */
                | (0x1 << 2)    /* GOC: write operation */
                | (1U  << 0);   /* MB: busy, starts transfer */

    while (ETH->MACMDIOAR & (1U << 0));

    /* Wait for reset bit to self-clear */
    for (volatile int i = 0; i < 1000000; i++);

    /* ===================== WAIT FOR LINK UP ===================== */

    /* Poll PHY basic status register (reg 1) bit 2 = link status */
    volatile uint32_t phy_bsr = 0;
    do {
        ETH->MACMDIOAR = (ETH->MACMDIOAR & ~(0x1F << 21 | 0x1F << 16 | 0x3 << 2))
                    | (0U  << 21)
                    | (1U  << 16)   /* RDA: register 1 - basic status */
                    | (0x3 << 2)
                    | (1U  << 0);

        while (ETH->MACMDIOAR & (1U << 0));
        phy_bsr = ETH->MACMDIODR & 0xFFFF;
    } while (!(phy_bsr & (1U << 2)));  /* wait for link up bit */

    /* ===================== CHECK NEGOTIATED SPEED ===================== */

    /* Read PHY special control/status register (reg 31) for speed/duplex result */
    ETH->MACMDIOAR = (ETH->MACMDIOAR & ~(0x1F << 21 | 0x1F << 16 | 0x3 << 2))
                | (0U  << 21)
                | (31U << 16)   /* RDA: register 31 - special status */
                | (0x3 << 2)
                | (1U  << 0);

    while (ETH->MACMDIOAR & (1U << 0));

    volatile uint32_t phy_scsr = ETH->MACMDIODR & 0xFFFF;

    /* bits 4:2 of SCSR indicate speed:
    001 = 10Mbps half
    101 = 10Mbps full
    010 = 100Mbps half
    110 = 100Mbps full */
    volatile uint32_t speed_bits = (phy_scsr >> 2) & 0x7;


    /* Software reset ETH DMA */
    ETH->DMAMR |= ETH_DMAMR_SWR;
    while (ETH->DMAMR & ETH_DMAMR_SWR);

    /* Keep MDC in-spec after reset (CR gets reset with SWR). */
    ETH->MACMDIOAR = (ETH->MACMDIOAR & ~ETH_MACMDIOAR_CR) | ETH_MACMDIOAR_CR_DIV124;

    /* DMA bus mode - address aligned beats, fixed burst */
    ETH->DMASBMR |= ETH_DMASBMR_AAL | ETH_DMASBMR_FB;

    /* MAC config: 100Mbps, full duplex */
    ETH->MACCR = ETH_MACCR_FES    /* 100Mbps */
               | ETH_MACCR_DM;    /* full duplex */

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

    /* Clear sticky DMA status. */
    ETH->DMACSR = ETH_DMACSR_NIS
                | ETH_DMACSR_AIS
                | ETH_DMACSR_CDE
                | ETH_DMACSR_FBE
                | ETH_DMACSR_ERI
                | ETH_DMACSR_ETI
                | ETH_DMACSR_RWT
                | ETH_DMACSR_RPS
                | ETH_DMACSR_RBU
                | ETH_DMACSR_RI
                | ETH_DMACSR_TBU
                | ETH_DMACSR_TPS
                | ETH_DMACSR_TI;

    /* Polling mode: DMA interrupts disabled. */
    ETH->DMACIER = 0U;

    /* Start DMA TX and RX */
    ETH->DMACTCR |= (1U << 0);   /* ST: start TX */
    ETH->DMACRCR |= (1U << 0);   /* SR: start RX */

    return ERR_OK;

}

static err_t ethernetif_output(struct netif *netif, struct pbuf *p)
{
    (void)netif;

    if ((p == NULL) || (p->tot_len == 0U))
    {
        return ERR_ARG;
    }

    if ((p->tot_len > ETH_BUFFER_SIZE) || (p->tot_len > ETH_TX_DESC_FL_MASK))
    {
        return ERR_IF;
    }

    ETH_DMADescTypeDef *desc = &TxDescriptors[TxDescIdx];

    /* Bound the wait: never hard-lock the CPU if TX ownership gets stuck. */
    uint32_t wait = ETH_TX_WAIT_SPINS;
    while ((desc->DESC3 & ETH_TX_DESC_OWN) != 0U)
    {
        if (wait-- == 0U)
        {
            uint32_t dmacsr = ETH->DMACSR;
            uint32_t clear = dmacsr & (ETH_DMACSR_NIS
                                     | ETH_DMACSR_AIS
                                     | ETH_DMACSR_FBE
                                     | ETH_DMACSR_TBU
                                     | ETH_DMACSR_TPS
                                     | ETH_DMACSR_TI);
            if (clear != 0U)
            {
                ETH->DMACSR = clear;
            }

            /* Re-kick TX DMA in case it stopped at a boundary/underflow. */
            ETH->DMACTCR |= ETH_DMACTCR_ST;
            ETH->DMACTDTPR = (uint32_t)&TxDescriptors[TxDescIdx];
            return ERR_MEM;
        }
    }

    /* Copy pbuf chain into TX buffer */
    uint8_t *buf = TxBuffers[TxDescIdx];
    uint16_t len = 0U;
    for (struct pbuf *q = p; q != NULL; q = q->next)
    {
        if ((uint32_t)len + (uint32_t)q->len > ETH_BUFFER_SIZE)
        {
            return ERR_IF;
        }

        memcpy(buf + len, q->payload, q->len);
        len += q->len;
    }

    /* Set up TX descriptor */
    desc->DESC0 = (uint32_t)buf;
    desc->DESC1 = 0;
    desc->DESC2 = ((uint32_t)len & ETH_TX_DESC_B1L_MASK); /* buffer 1 length */
    desc->DESC3 = ETH_TX_DESC_FD
                | ETH_TX_DESC_LD
                | ETH_TX_CIC_FULL
                | ((uint32_t)len & ETH_TX_DESC_FL_MASK);

    /* Ensure descriptor words are visible before ownership handover to DMA. */
    __DMB();
    desc->DESC3 |= ETH_TX_DESC_OWN;

    /* Advance index */
    TxDescIdx = (TxDescIdx + 1) % ETH_TX_DESC_COUNT;

    /* Poll demand - wake up TX DMA */
    ETH->DMACTDTPR = (uint32_t)&TxDescriptors[TxDescIdx];
    ETH->DMACTCR |= ETH_DMACTCR_ST;

    return ERR_OK;
}

static void ethernetif_input(struct netif *netif)
{
    ETH_DMADescTypeDef *desc = &RxDescriptors[RxDescIdx];

    /* Check if DMA has written a frame - with frame count limit */
    int frame_count = 0;
    uint32_t returned_idx = 0U;
    bool returned_any = false;
    while (!(desc->DESC3 & (1U << 31)) && frame_count < 100)
    {
        frame_count++;
        uint32_t rdes3 = desc->DESC3;
        uint16_t len = rdes3 & 0x7FFF;  /* frame length */

        if ((len > 0U) && (len <= ETH_BUFFER_SIZE))
        {
            struct pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
            if (p != NULL)
            {
                uint16_t copied = 0U;
                for (struct pbuf *q = p; q != NULL; q = q->next)
                {
                    memcpy(q->payload, &RxBuffers[RxDescIdx][copied], q->len);
                    copied += q->len;
                }

                if (netif->input(p, netif) != ERR_OK)
                {
                    pbuf_free(p);
                }
            }
        }

        /* Give descriptor back to DMA */
        desc->DESC0 = (uint32_t)RxBuffers[RxDescIdx];
        desc->DESC1 = 0;
        desc->DESC2 = ETH_BUFFER_SIZE;
        __DMB();
        desc->DESC3 = (1U << 31) | (1U << 30) | (1U << 24);

        returned_idx = RxDescIdx;
        returned_any = true;

        /* Advance software index */
        RxDescIdx = (RxDescIdx + 1) % ETH_RX_DESC_COUNT;
        desc = &RxDescriptors[RxDescIdx];
    }

    if (returned_any)
    {
        /* Tell DMA the last descriptor we just returned. */
        __DMB();
        ETH->DMACRDTPR = (uint32_t)&RxDescriptors[returned_idx];
    }

    volatile uint32_t rx_frames = frame_count;
    (void)rx_frames;

}

static void ethernetif_service_dma()
{
    uint32_t dmacsr = ETH->DMACSR;
    uint32_t clear = dmacsr & (ETH_DMACSR_NIS
                             | ETH_DMACSR_AIS
                             | ETH_DMACSR_FBE
                             | ETH_DMACSR_RBU
                             | ETH_DMACSR_RPS
                             | ETH_DMACSR_RI
                             | ETH_DMACSR_TBU
                             | ETH_DMACSR_TPS
                             | ETH_DMACSR_TI);

    if (clear != 0U)
    {
        ETH->DMACSR = clear;
    }

    if ((dmacsr & (ETH_DMACSR_TPS | ETH_DMACSR_TBU)) != 0U)
    {
        ETH->DMACTCR |= ETH_DMACTCR_ST;
        ETH->DMACTDTPR = (uint32_t)&TxDescriptors[TxDescIdx];
    }

    if ((dmacsr & (ETH_DMACSR_RPS | ETH_DMACSR_RBU)) != 0U)
    {
        ETH->DMACRCR |= ETH_DMACRCR_SR;
        ETH->DMACRDTPR = (uint32_t)&RxDescriptors[(RxDescIdx + ETH_RX_DESC_COUNT - 1U) % ETH_RX_DESC_COUNT];
    }
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
    if (!ethernetif_read_phy(ETH_PHY_REG_BSR, &bsr))
    {
        return;
    }

    /* BSR link bit is latch-low: read twice and use the second read. */
    if (!ethernetif_read_phy(ETH_PHY_REG_BSR, &bsr))
    {
        return;
    }

    /* Extract link status from BSR bit 2 */
    uint32_t link_state = (bsr & ETH_PHY_BSR_LINK) >> 2;

    /* Detect state change */
    if (link_state != link_state_last)
    {
        link_state_last = link_state;

        if (link_state)
        {
            /* Link is up - apply negotiated mode */
            ethernetif_apply_link_mode();
            netif_set_link_up(&eth_netif);
            //dhcp_start(&eth_netif);
        }
        else
        {
            /* Link is down */
            netif_set_link_down(&eth_netif);
            //dhcp_stop(&eth_netif);
        }
    }
}

/*======================================================================
 * UDP Callback Handler (called by lwIP when UDP data arrives)
 *======================================================================*/
static void eth_udp_recv_handler(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                                  const ip_addr_t *addr, uint16_t port)
{
    (void)arg;
    (void)pcb;

    if (p == NULL)
    {
        return;
    }

    if (udp_recv_callback != NULL)
    {
        /* Call application callback with payload data */
        udp_recv_callback((const uint8_t *)p->payload, p->tot_len, addr->addr, port);
    }

    pbuf_free(p);
}

static err_t eth_tcp_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    (void)arg;

    if ((err != ERR_OK) || (newpcb == NULL))
    {
        return ERR_VAL;
    }

    if (tcp_client_pcb != NULL)
    {
        tcp_abort(newpcb);
        return ERR_ABRT;
    }

    tcp_client_pcb = newpcb;
    tcp_rx_len = 0U;
    tcp_client_port = newpcb->remote_port;

    tcp_arg(newpcb, NULL);
    tcp_recv(newpcb, eth_tcp_recv_cb);
    tcp_poll(newpcb, eth_tcp_poll_cb, 4U);
    tcp_err(newpcb, eth_tcp_err_cb);

    eth_log("TCP client connected");
    eth_log_ip4("TCP client IP", newpcb->remote_ip.addr);
    eth_log_u32("TCP client port", newpcb->remote_port);

    return ERR_OK;
}

static err_t eth_tcp_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    (void)arg;

    if ((err != ERR_OK) || (tpcb != tcp_client_pcb))
    {
        if (p != NULL)
        {
            pbuf_free(p);
        }
        eth_tcp_close_client();
        return ERR_OK;
    }

    if (p == NULL)
    {
        eth_log("TCP client disconnected");
        eth_tcp_close_client();
        return ERR_OK;
    }

    tcp_recved(tpcb, p->tot_len);

    for (struct pbuf *q = p; q != NULL; q = q->next)
    {
        uint16_t space = (uint16_t)(ETH_TCP_RX_BUFFER_SIZE - tcp_rx_len);
        if (q->len > space)
        {
            eth_log("TCP RX buffer overflow");
            pbuf_free(p);
            eth_tcp_close_client();
            return ERR_OK;
        }

        memcpy(&tcp_rx_buffer[tcp_rx_len], q->payload, q->len);
        tcp_rx_len = (uint16_t)(tcp_rx_len + q->len);

        if (tcp_recv_callback != NULL)
        {
            tcp_recv_callback((const uint8_t *)q->payload, q->len, tpcb->remote_ip.addr, tpcb->remote_port);
        }
    }

    pbuf_free(p);
    tcp_rx_len = 0U;

    return ERR_OK;
}

static err_t eth_tcp_poll_cb(void *arg, struct tcp_pcb *tpcb)
{
    (void)arg;
    (void)tpcb;
    return ERR_OK;
}

static void eth_tcp_err_cb(void *arg, err_t err)
{
    (void)arg;
    (void)err;
    tcp_client_pcb = NULL;
    tcp_client_port = 0U;
    tcp_rx_len = 0U;
    eth_log("TCP client aborted");
}

static void eth_tcp_close_client(void)
{
    if (tcp_client_pcb != NULL)
    {
        struct tcp_pcb *pcb = tcp_client_pcb;
        tcp_arg(pcb, NULL);
        tcp_recv(pcb, NULL);
        tcp_sent(pcb, NULL);
        tcp_poll(pcb, NULL, 0);
        tcp_err(pcb, NULL);

        if (tcp_close(pcb) != ERR_OK)
        {
            tcp_abort(pcb);
        }
    }

    tcp_client_pcb = NULL;
    tcp_client_port = 0U;
    tcp_rx_len = 0U;
}

static eth_result_t eth_log_send(const uint8_t *data, uint16_t len)
{
    if ((!log_enabled) || (log_udp_pcb == NULL) || (data == NULL) || (len == 0U))
    {
        return ETH_RES_ERR;
    }

    struct pbuf *pkt = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
    if (pkt == NULL)
    {
        return ETH_RES_ERR;
    }

    pbuf_take(pkt, data, len);
    err_t err = udp_sendto(log_udp_pcb, pkt, &log_dst_addr, log_dst_port);
    pbuf_free(pkt);
    return (err == ERR_OK) ? ETH_RES_OK : ETH_RES_ERR;
}

static void eth_log_write_prefix(void)
{
    uint32_t total_us = sys_micros();
    uint32_t seconds = total_us / 1000000U;
    uint32_t micros = total_us % 1000000U;
    uint32_t millis = micros / 1000U;
    uint32_t micro_rem = micros % 1000U;

    eth_log_puts("[NCU ");
    eth_log_put_u32(seconds);
    eth_log_putc_raw('.');
    eth_log_put_fixed3(millis);
    eth_log_putc_raw('.');
    eth_log_put_fixed3(micro_rem);
    eth_log_puts("] ");
}

static void eth_log_write_kv_sep(const char *label)
{
    eth_log_puts((label != NULL) ? label : "log");
    eth_log_puts(": ");
}

static void eth_log_ip4(const char *label, uint32_t addr)
{
    uint32_t a = addr & 0xFFU;
    uint32_t b = (addr >> 8) & 0xFFU;
    uint32_t c = (addr >> 16) & 0xFFU;
    uint32_t d = (addr >> 24) & 0xFFU;

    eth_log_write_prefix();
    eth_log_write_kv_sep((label != NULL) ? label : "ip4");
    eth_log_put_u32(a);
    eth_log_putc_raw('.');
    eth_log_put_u32(b);
    eth_log_putc_raw('.');
    eth_log_put_u32(c);
    eth_log_putc_raw('.');
    eth_log_put_u32(d);
    eth_log_puts("\r\n");
}

static void eth_log_stdio_flush(void)
{
    if ((!log_enabled) || (log_udp_pcb == NULL))
    {
        return;
    }

    uint8_t line[ETH_LOG_LINE_MAX];
    uint16_t line_len = 0U;
    uint32_t drained = 0U;

    while (drained < ETH_STDIO_RING_SIZE)
    {
        uint8_t ch = 0U;
        bool have_char = false;

        uint32_t primask = __get_PRIMASK();
        __disable_irq();
        if (stdio_tail != stdio_head)
        {
            ch = stdio_ring[stdio_tail];
            stdio_tail = (uint16_t)((stdio_tail + 1U) % ETH_STDIO_RING_SIZE);
            have_char = true;
        }
        __set_PRIMASK(primask);

        if (!have_char)
        {
            break;
        }

        drained++;

        if (ch == '\r')
        {
            continue;
        }

        line[line_len++] = ch;
        if ((ch == '\n') || (line_len >= ETH_LOG_LINE_MAX))
        {
            (void)eth_log_send(line, line_len);
            line_len = 0U;
        }
    }

    if (line_len > 0U)
    {
        (void)eth_log_send(line, line_len);
    }
}

static void eth_log_puts(const char *s)
{
    if (s == NULL)
    {
        return;
    }

    while (*s != '\0')
    {
        eth_log_putc_raw((uint8_t)*s++);
    }
}

static void eth_log_put_u32(uint32_t value)
{
    char tmp[10];
    uint32_t n = 0U;

    do
    {
        tmp[n++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while ((value > 0U) && (n < sizeof(tmp)));

    while (n > 0U)
    {
        n--;
        eth_log_putc_raw((uint8_t)tmp[n]);
    }
}

static void eth_log_put_i32(int32_t value)
{
    if (value < 0)
    {
        eth_log_putc_raw('-');
        uint32_t mag = (uint32_t)(-(value + 1)) + 1U;
        eth_log_put_u32(mag);
        return;
    }

    eth_log_put_u32((uint32_t)value);
}

static void eth_log_put_fixed3(uint32_t value)
{
    uint32_t v = value % 1000U;
    eth_log_putc_raw((uint8_t)('0' + ((v / 100U) % 10U)));
    eth_log_putc_raw((uint8_t)('0' + ((v / 10U) % 10U)));
    eth_log_putc_raw((uint8_t)('0' + (v % 10U)));
}

