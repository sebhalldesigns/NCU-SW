/***************************************************************
**
** NCU Header File
**
** File         :  eth.h
** Module       :  eth
** Author       :  SH
** Created      :  2026-03-28 (YYYY-MM-DD)
** License      :  MIT
** Description  :  NCU Ethernet Interface
**
***************************************************************/

#ifndef ETH_H
#define ETH_H

/***************************************************************
** MARK: INCLUDES
***************************************************************/

#include <sys/sys.h>



/***************************************************************
** MARK: CONSTANTS & MACROS
***************************************************************/

/***************************************************************
** MARK: TYPEDEFS
***************************************************************/

/* UDP receive callback: called when data arrives */
typedef void (*eth_udp_recv_callback_t)(const uint8_t *data, uint16_t len, uint32_t src_ip, uint16_t src_port);
/* WebSocket receive callback: called when a complete WS frame is received. */
typedef void (*eth_ws_recv_callback_t)(const uint8_t *data, uint16_t len, bool is_text);

/***************************************************************
** MARK: FUNCTION DEFS
***************************************************************/

bool eth_init();

void eth_poll();

/* UDP API (callback-based for NO_SYS mode) */
int eth_udp_bind(uint16_t port, eth_udp_recv_callback_t recv_callback);

int eth_udp_send(uint32_t dst_ip, uint16_t dst_port, const uint8_t *data, uint16_t len);

/* UDP debug log API (non-blocking, buffered, flushes inside eth_poll) */
bool eth_dbg_init(uint32_t dst_ip, uint16_t dst_port);
int eth_dbg_send(const uint8_t *data, uint16_t len);
void eth_dbg_printf(const char *fmt, ...);

/* WebSocket API (server mode, raw lwIP TCP) */
int eth_ws_init(uint16_t port, eth_ws_recv_callback_t recv_callback);
bool eth_ws_is_connected(void);
int eth_ws_send_text(const uint8_t *data, uint16_t len);
int eth_ws_send_binary(const uint8_t *data, uint16_t len);

/* Interrupt flag: set by ETH_IRQHandler, cleared by main loop */
extern volatile int eth_packet_ready;

#endif /* ETH_H */
