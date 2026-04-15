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
/* TCP receive callback: called when stream data arrives. */
typedef void (*eth_tcp_recv_callback_t)(const uint8_t *data, uint16_t len, uint32_t src_ip, uint16_t src_port);

/* Common Ethernet API status code. */
typedef enum
{
    ETH_RES_ERR = -1,
    ETH_RES_OK  = 0
} eth_result_t;

/***************************************************************
** MARK: FUNCTION DEFS
***************************************************************/

bool eth_init();

/* Call frequently from non-ISR context (main loop/high-priority task). */
void eth_poll();

/* UDP API (callback-based for NO_SYS mode). */
eth_result_t eth_udp_bind(uint16_t port, eth_udp_recv_callback_t recv_callback);

eth_result_t eth_udp_send(uint32_t dst_ip, uint16_t dst_port, const uint8_t *data, uint16_t len);

/* Logging API (separate UDP PCB from application UDP).
 * Output format: [NCU sss.mmm.uuu] label: value
 */
bool eth_log_init(uint32_t dst_ip, uint16_t dst_port);
void eth_log(const char *content);
void eth_log_u32(const char *label, uint32_t value);
void eth_log_i32(const char *label, int32_t value);
void eth_log_bool(const char *label, bool value);

/* Internal raw-byte sink (used by __io_putchar); flushed from eth_poll(). */
void eth_log_putc_raw(uint8_t ch);

/* WebSocket API (server mode, raw lwIP TCP) */
eth_result_t eth_ws_init(uint16_t port, eth_ws_recv_callback_t recv_callback);
bool eth_ws_is_connected(void);
eth_result_t eth_ws_send_text(const uint8_t *data, uint16_t len);
eth_result_t eth_ws_send_binary(const uint8_t *data, uint16_t len);

/* TCP API (server mode, raw lwIP TCP) */
eth_result_t eth_tcp_init(uint16_t port, eth_tcp_recv_callback_t recv_callback);
bool eth_tcp_is_connected(void);
eth_result_t eth_tcp_send(const uint8_t *data, uint16_t len);

/* Interrupt flag: set by ETH_IRQHandler, cleared by main loop */
extern volatile int eth_packet_ready;

#endif /* ETH_H */
