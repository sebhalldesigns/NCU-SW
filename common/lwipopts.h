#ifndef LWIPOPTS_H
#define LWIPOPTS_H

/* ---- Core operating mode ---- */
#define NO_SYS                      1   /* No RTOS */
#define LWIP_TIMERS                 1   /* Required in NO_SYS mode */
#define LWIP_NETIF_API              0   /* No netif API in NO_SYS */

/* ---- Memory ---- */
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    (16 * 1024)  /* 16KB heap */
#define MEMP_NUM_PBUF               16
#define MEMP_NUM_TCP_PCB            4
#define MEMP_NUM_TCP_PCB_LISTEN     2
#define MEMP_NUM_TCP_SEG            16
#define PBUF_POOL_SIZE              8

/* ---- TCP ---- */
#define LWIP_TCP                    1
#define TCP_WND                     (4 * TCP_MSS)
#define TCP_MSS                     1460
#define TCP_SND_BUF                 (4 * TCP_MSS)
#define TCP_SND_QUEUELEN            8

/* ---- UDP (needed internally by lwIP) ---- */
#define LWIP_UDP                    1

/* ---- ICMP (ping support) ---- */
#define LWIP_ICMP                   1

/* ---- DHCP ---- */
#define LWIP_DHCP                   1

/* ---- Ethernet ---- */
#define LWIP_ARP                    1
#define ETH_PAD_SIZE                0

/* ---- Checksum: let hardware do it where possible ---- */
#define CHECKSUM_BY_HARDWARE        1
#ifdef CHECKSUM_BY_HARDWARE
  #define CHECKSUM_GEN_IP           0
  #define CHECKSUM_GEN_UDP          0
  #define CHECKSUM_GEN_TCP          0
  #define CHECKSUM_CHECK_IP         0
  #define CHECKSUM_CHECK_UDP        0
  #define CHECKSUM_CHECK_TCP        0
#endif

/* ---- Stats (disable for production, useful for debug) ---- */
#define LWIP_STATS                  0

/* ---- Debug (enable selectively when needed) ---- */
#define LWIP_DEBUG                  0

/* Disable Sequential/netconn API - not needed in NO_SYS mode */
#define LWIP_NETCONN                0
#define LWIP_SOCKET                 0

#endif /* LWIPOPTS_H */