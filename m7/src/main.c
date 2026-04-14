
#include <sys/sys.h>
#include <task/task.h>
#include <eth/eth.h>
#include <lwip/ip4_addr.h>

#define UDP_ECHO_PORT 5005U
#define WS_PORT 81U
#define LOG_PORT 50000U

volatile uint8_t led_state = 0;
volatile uint32_t udp_rx_packets = 0U;
volatile uint32_t udp_tx_failures = 0U;
volatile uint32_t ws_rx_packets = 0U;
volatile uint32_t ws_tx_failures = 0U;

static void udp_echo_callback(const uint8_t *data, uint16_t len, uint32_t src_ip, uint16_t src_port)
{
    udp_rx_packets++;

    if ((data == NULL) || (len == 0U))
    {
        return;
    }

    if (eth_udp_send(src_ip, src_port, data, len) != ETH_RES_OK)
    {
        udp_tx_failures++;
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
    
}

void task_b(uint32_t time_us)
{
    //eth_log_u32("TASK B", time_us);
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

int main(void)
{
    sys_init();
    eth_init();

    ip4_addr_t log_ip4;
    IP4_ADDR(&log_ip4, 192, 168, 1, 51);
    (void)eth_log_init(log_ip4.addr, LOG_PORT);

    eth_udp_bind(UDP_ECHO_PORT, udp_echo_callback);
    eth_ws_init(WS_PORT, ws_echo_callback);

    eth_log("Welcome to NCU");

    task_init(500, 5000); /* Task A at 500us, Task B at 1s */
    
    task_register(M7_TASK_A, task_a);
    task_register(M7_TASK_B, task_b);
    task_register(M7_TASK_C, task_c);

    task_run();
}
