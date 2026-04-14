
#include "xcp/xcp.h"
#include <stdint.h>
#include <sys/sys.h>
#include <task/task.h>
#include <xcp/xcp.h>

#include <eth/eth.h>
#include <lwip/ip4_addr.h>

#define UDP_ECHO_PORT 5005U
#define WS_PORT 81U
#define LOG_PORT 50000U
#define XCP_MAX_UDP_PACKET_SIZE (XCP_IP_HEADER_SIZE + XCP_MAX_FRAME_SIZE)
#define HEX_BUF_SIZE ((XCP_MAX_UDP_PACKET_SIZE * 3U) + 1U)

volatile uint8_t led_state = 0;
volatile uint32_t udp_rx_packets = 0U;
volatile uint32_t udp_tx_failures = 0U;
volatile uint32_t ws_rx_packets = 0U;
volatile uint32_t ws_tx_failures = 0U;

static const char hex_chars[] = "0123456789ABCDEF";

static void udp_echo_callback(const uint8_t *data, uint16_t len, uint32_t src_ip, uint16_t src_port)
{
    eth_log("UDP RX");

    if (len <= XCP_MAX_UDP_PACKET_SIZE && len >= (XCP_IP_HEADER_SIZE + 1U))
    {
        char hex_buf[HEX_BUF_SIZE];
        uint8_t frame_len = (uint8_t)(data[0] | (data[1] << 8));
        if (frame_len == 0U || frame_len > XCP_MAX_FRAME_SIZE || (uint16_t)frame_len != (len - XCP_IP_HEADER_SIZE))
        {
            eth_log("UDP RX invalid XCP length");
            return;
        }

        for (uint16_t i = 0; i < len; i++)
        {
            uint8_t byte = data[i];
            hex_buf[i * 3] = hex_chars[byte >> 4];
            hex_buf[i * 3 + 1] = hex_chars[byte & 0x0F];
            hex_buf[i * 3 + 2] = ' ';
        }

        hex_buf[len * 3] = '\0';
        eth_log(hex_buf);

        
        static xcp_conn_info_t conn_info = {0}; 
        conn_info.ip.remote_ip = src_ip;
        conn_info.ip.remote_port = src_port;
        conn_info.ip.counter = data[2] | (data[3] << 8);

        static xcp_frame_t frame;
        frame.length = frame_len;
        frame.pid = data[4];
        for (uint8_t i = 0; i < (uint8_t)(frame.length - 1U); i++)
        {
            frame.data[i] = data[5 + i];
        }

        xcp_receive_frame(XCP_CONN_TYPE_ETH_UDP, &conn_info, &frame);
    }

}

static void ws_echo_callback(const uint8_t *data, uint16_t len, bool is_text)
{
    ws_rx_packets++;

    if ((data == NULL) || (len == 0U))
    {
        return;
    }

    if (is_text)
    {
        if (eth_ws_send_text(data, len) != ETH_RES_OK)
        {
            ws_tx_failures++;
        }
    }
    else
    {
        if (eth_ws_send_binary(data, len) != ETH_RES_OK)
        {
            ws_tx_failures++;
        }
    }
}

void task_a(uint32_t time_us)
{
    xcp_update();
}

void task_b(uint32_t time_us)
{
    static uint32_t last_log_time_us = 0;
    if (time_us - last_log_time_us >= 1000000) /* Log every 1 second */
    {
        last_log_time_us = time_us;
        eth_log_u32("TASK B", time_us);
    }
    
}

void task_c(uint32_t time_us)
{
    (void)time_us;
    if (eth_packet_ready)
    {
        eth_packet_ready = 0;
    }

    eth_poll();
}

void xcp_eth_udp_response_handler(xcp_conn_info_t *conn_info, xcp_frame_t *response_frame)
{
    if ((conn_info == NULL) || (response_frame == NULL))
    {
        return;
    }

    if (response_frame->length == 0U || response_frame->length > XCP_MAX_FRAME_SIZE)
    {
        eth_log("UDP TX invalid XCP length");
        return;
    }

    static uint8_t response[XCP_MAX_FRAME_SIZE + XCP_IP_HEADER_SIZE];
    char hex_buf[HEX_BUF_SIZE];
    
    response[0] = response_frame->length & 0xFF;
    response[1] = (response_frame->length >> 8) & 0xFF;
    
    response[2] = conn_info->ip.counter & 0xFF;
    response[3] = (conn_info->ip.counter >> 8) & 0xFF;
    
    response[4] = response_frame->pid;

    for (uint8_t i = 0; i < (uint8_t)(response_frame->length - 1U); i++)
    {
        response[5 + i] = response_frame->data[i];
    }

    for (uint16_t i = 0; i < response_frame->length + XCP_IP_HEADER_SIZE; i++)
    {
        uint8_t byte = response[i];
        hex_buf[i * 3] = hex_chars[byte >> 4];
        hex_buf[i * 3 + 1] = hex_chars[byte & 0x0F];
        hex_buf[i * 3 + 2] = ' ';
    }

    hex_buf[response_frame->length * 3 + XCP_IP_HEADER_SIZE * 3] = '\0';
    eth_log("UDP TX");
    eth_log(hex_buf);




    eth_udp_send(conn_info->ip.remote_ip, conn_info->ip.remote_port, response, (uint16_t)(response_frame->length + XCP_IP_HEADER_SIZE));
}

int main(void)
{
    sys_init();
    xcp_init();
    xcp_add_response_handler(XCP_CONN_TYPE_ETH_UDP, xcp_eth_udp_response_handler);

    eth_init();

    ip4_addr_t log_ip4;
    IP4_ADDR(&log_ip4, 192, 168, 1, 51);
    (void)eth_log_init(log_ip4.addr, LOG_PORT);

    eth_udp_bind(UDP_ECHO_PORT, udp_echo_callback);
    eth_ws_init(WS_PORT, ws_echo_callback);

    eth_log("NCU Initialization Complete");

    task_init(500, 5000); /* Task A at 500us, Task B at 1s */
    
    task_register(M7_TASK_A, task_a);
    task_register(M7_TASK_B, task_b);
    task_register(M7_TASK_C, task_c);

    task_run();
}
