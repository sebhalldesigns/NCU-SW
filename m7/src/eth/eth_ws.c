/***************************************************************
**
** NCU Source File
**
** File         :  eth_ws.c
** Module       :  eth
** Author       :  SH
** Created      :  2026-03-28 (YYYY-MM-DD)
** License      :  MIT
** Description  :  WebSocket server over lwIP raw TCP API
**
***************************************************************/

#include "eth.h"

#include <lwip/tcp.h>

#include <string.h>

/***************************************************************
** MARK: CONSTANTS & MACROS
***************************************************************/

#define ETH_WS_DEFAULT_PORT      81U
#define ETH_WS_RX_BUFFER_SIZE    2048U
#define ETH_WS_GUID              "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

#define WS_OPCODE_TEXT           0x1U
#define WS_OPCODE_BINARY         0x2U
#define WS_OPCODE_CLOSE          0x8U
#define WS_OPCODE_PING           0x9U
#define WS_OPCODE_PONG           0xAU

/***************************************************************
** MARK: TYPEDEFS
***************************************************************/

typedef enum
{
    ETH_WS_STATE_IDLE = 0,
    ETH_WS_STATE_HANDSHAKE,
    ETH_WS_STATE_OPEN
} eth_ws_state_t;

typedef struct
{
    uint32_t state[5];
    uint64_t bit_count;
    uint8_t buffer[64];
} eth_ws_sha1_ctx_t;

/***************************************************************
** MARK: STATIC VARIABLES
***************************************************************/

static struct tcp_pcb *ws_listener_pcb = NULL;
static struct tcp_pcb *ws_client_pcb = NULL;
static eth_ws_state_t ws_state = ETH_WS_STATE_IDLE;
static uint16_t ws_listen_port = ETH_WS_DEFAULT_PORT;
static eth_ws_recv_callback_t ws_recv_callback = NULL;

static uint8_t ws_rx_buffer[ETH_WS_RX_BUFFER_SIZE];
static uint16_t ws_rx_len = 0U;
static uint32_t ws_sha1_w[80];
static const uint8_t ws_sha1_zero_block[64] = {0U};
static char ws_client_key[64];
static char ws_accept_key[48];
static eth_ws_sha1_ctx_t ws_sha1_ctx;

/***************************************************************
** MARK: STATIC FUNCTION DEFS
***************************************************************/

static void eth_ws_sha1_init(eth_ws_sha1_ctx_t *ctx);
static void eth_ws_sha1_update(eth_ws_sha1_ctx_t *ctx, const uint8_t *data, uint32_t len);
static void eth_ws_sha1_final(eth_ws_sha1_ctx_t *ctx, uint8_t digest[20]);
static void eth_ws_sha1_transform(uint32_t state[5], const uint8_t block[64]);

static void eth_ws_base64_encode(const uint8_t *input, uint16_t len, char *output, uint16_t output_size);
static int eth_ws_build_accept_key(const char *client_key, char *accept_key, uint16_t accept_key_size);

static void eth_ws_reset_client_state(void);
static void eth_ws_close_client(void);
static void eth_ws_consume_rx(uint16_t count);
static int eth_ws_handle_handshake(struct tcp_pcb *tpcb);
static int eth_ws_handle_frame(void);
static int eth_ws_send_frame(uint8_t opcode, const uint8_t *data, uint16_t len);

static err_t eth_ws_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err);
static err_t eth_ws_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
static err_t eth_ws_poll_cb(void *arg, struct tcp_pcb *tpcb);
static void eth_ws_err_cb(void *arg, err_t err);

/***************************************************************
** MARK: SHA1
***************************************************************/

static uint32_t eth_ws_sha1_rotl(uint32_t value, uint32_t bits)
{
    return (value << bits) | (value >> (32U - bits));
}

static void eth_ws_sha1_transform(uint32_t state[5], const uint8_t block[64])
{
    uint32_t *w = ws_sha1_w;
    for (uint32_t i = 0; i < 16U; i++)
    {
        w[i] = ((uint32_t)block[i * 4U] << 24)
             | ((uint32_t)block[i * 4U + 1U] << 16)
             | ((uint32_t)block[i * 4U + 2U] << 8)
             | ((uint32_t)block[i * 4U + 3U]);
    }

    for (uint32_t i = 16U; i < 80U; i++)
    {
        w[i] = eth_ws_sha1_rotl(w[i - 3U] ^ w[i - 8U] ^ w[i - 14U] ^ w[i - 16U], 1U);
    }

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];

    for (uint32_t i = 0; i < 80U; i++)
    {
        uint32_t f;
        uint32_t k;
        if (i < 20U)
        {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999U;
        }
        else if (i < 40U)
        {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1U;
        }
        else if (i < 60U)
        {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCU;
        }
        else
        {
            f = b ^ c ^ d;
            k = 0xCA62C1D6U;
        }

        uint32_t temp = eth_ws_sha1_rotl(a, 5U) + f + e + k + w[i];
        e = d;
        d = c;
        c = eth_ws_sha1_rotl(b, 30U);
        b = a;
        a = temp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

static void eth_ws_sha1_init(eth_ws_sha1_ctx_t *ctx)
{
    ctx->state[0] = 0x67452301U;
    ctx->state[1] = 0xEFCDAB89U;
    ctx->state[2] = 0x98BADCFEU;
    ctx->state[3] = 0x10325476U;
    ctx->state[4] = 0xC3D2E1F0U;
    ctx->bit_count = 0U;
}

static void eth_ws_sha1_update(eth_ws_sha1_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    if ((ctx == NULL) || (data == NULL) || (len == 0U))
    {
        return;
    }

    uint32_t buffer_index = (uint32_t)((ctx->bit_count >> 3) & 0x3FU);
    ctx->bit_count += ((uint64_t)len << 3);

    uint32_t i = 0U;
    if ((buffer_index + len) >= 64U)
    {
        uint32_t fill = 64U - buffer_index;
        memcpy(&ctx->buffer[buffer_index], data, fill);
        eth_ws_sha1_transform(ctx->state, ctx->buffer);
        i = fill;
        while ((i + 63U) < len)
        {
            eth_ws_sha1_transform(ctx->state, &data[i]);
            i += 64U;
        }
        buffer_index = 0U;
    }

    if (i < len)
    {
        memcpy(&ctx->buffer[buffer_index], &data[i], len - i);
    }
}

static void eth_ws_sha1_final(eth_ws_sha1_ctx_t *ctx, uint8_t digest[20])
{
    uint8_t len_be[8];
    uint8_t pad_start = 0x80U;

    for (uint32_t i = 0; i < 8U; i++)
    {
        len_be[7U - i] = (uint8_t)((ctx->bit_count >> (i * 8U)) & 0xFFU);
    }

    uint32_t buffer_index = (uint32_t)((ctx->bit_count >> 3) & 0x3FU);
    uint32_t pad_len = (buffer_index < 56U) ? (56U - buffer_index) : (120U - buffer_index);
    eth_ws_sha1_update(ctx, &pad_start, 1U);
    if (pad_len > 1U)
    {
        eth_ws_sha1_update(ctx, ws_sha1_zero_block, pad_len - 1U);
    }
    eth_ws_sha1_update(ctx, len_be, 8U);

    for (uint32_t i = 0; i < 5U; i++)
    {
        digest[i * 4U]     = (uint8_t)((ctx->state[i] >> 24) & 0xFFU);
        digest[i * 4U + 1] = (uint8_t)((ctx->state[i] >> 16) & 0xFFU);
        digest[i * 4U + 2] = (uint8_t)((ctx->state[i] >> 8) & 0xFFU);
        digest[i * 4U + 3] = (uint8_t)(ctx->state[i] & 0xFFU);
    }
}

/***************************************************************
** MARK: BASE64 & HANDSHAKE
***************************************************************/

static void eth_ws_base64_encode(const uint8_t *input, uint16_t len, char *output, uint16_t output_size)
{
    static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint16_t in_idx = 0U;
    uint16_t out_idx = 0U;

    if ((output == NULL) || (output_size == 0U))
    {
        return;
    }

    while (in_idx < len)
    {
        uint16_t remaining = (uint16_t)(len - in_idx);
        uint32_t octet_a = input[in_idx];
        uint32_t octet_b = (remaining > 1U) ? input[in_idx + 1U] : 0U;
        uint32_t octet_c = (remaining > 2U) ? input[in_idx + 2U] : 0U;
        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;
        in_idx = (uint16_t)(in_idx + ((remaining >= 3U) ? 3U : remaining));

        if ((out_idx + 4U) >= output_size)
        {
            break;
        }

        output[out_idx++] = b64_table[(triple >> 18) & 0x3FU];
        output[out_idx++] = b64_table[(triple >> 12) & 0x3FU];
        output[out_idx++] = (remaining > 1U) ? b64_table[(triple >> 6) & 0x3FU] : '=';
        output[out_idx++] = (remaining > 2U) ? b64_table[triple & 0x3FU] : '=';
    }

    output[out_idx] = '\0';
}

static int eth_ws_build_accept_key(const char *client_key, char *accept_key, uint16_t accept_key_size)
{
    if ((client_key == NULL) || (accept_key == NULL))
    {
        return -1;
    }

    size_t key_len = strlen(client_key);
    if (key_len == 0U)
    {
        return -1;
    }

    uint8_t sha1[20];
    eth_ws_sha1_init(&ws_sha1_ctx);
    eth_ws_sha1_update(&ws_sha1_ctx, (const uint8_t *)client_key, (uint32_t)key_len);
    eth_ws_sha1_update(&ws_sha1_ctx, (const uint8_t *)ETH_WS_GUID, (uint32_t)(sizeof(ETH_WS_GUID) - 1U));
    eth_ws_sha1_final(&ws_sha1_ctx, sha1);

    eth_ws_base64_encode(sha1, sizeof(sha1), accept_key, accept_key_size);
    return 0;
}

/***************************************************************
** MARK: WEBSOCKET CORE
***************************************************************/

static void eth_ws_reset_client_state(void)
{
    ws_state = ETH_WS_STATE_IDLE;
    ws_rx_len = 0U;
}

static void eth_ws_close_client(void)
{
    if (ws_client_pcb != NULL)
    {
        struct tcp_pcb *pcb = ws_client_pcb;
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

    ws_client_pcb = NULL;
    eth_ws_reset_client_state();
}

static void eth_ws_consume_rx(uint16_t count)
{
    if ((count == 0U) || (count > ws_rx_len))
    {
        return;
    }

    uint16_t remain = (uint16_t)(ws_rx_len - count);
    if (remain > 0U)
    {
        memmove(ws_rx_buffer, &ws_rx_buffer[count], remain);
    }
    ws_rx_len = remain;
}

static int eth_ws_handle_handshake(struct tcp_pcb *tpcb)
{
    static const char key_prefix[] = "Sec-WebSocket-Key:";
    static const char response_prefix[] =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: ";
    static const char response_suffix[] = "\r\n\r\n";

    uint16_t header_end = 0xFFFFU;
    for (uint16_t i = 0U; (i + 3U) < ws_rx_len; i++)
    {
        if ((ws_rx_buffer[i] == '\r') && (ws_rx_buffer[i + 1U] == '\n') &&
            (ws_rx_buffer[i + 2U] == '\r') && (ws_rx_buffer[i + 3U] == '\n'))
        {
            header_end = i;
            break;
        }
    }

    if (header_end == 0xFFFFU)
    {
        return 0;
    }

    memset(ws_client_key, 0, sizeof(ws_client_key));
    bool key_found = false;
    size_t prefix_len = strlen(key_prefix);

    for (uint16_t i = 0U; i < header_end; i++)
    {
        bool line_start = (i == 0U) || (ws_rx_buffer[i - 1U] == '\n');
        if (!line_start)
        {
            continue;
        }

        if ((i + prefix_len) > header_end)
        {
            continue;
        }

        if (strncmp((const char *)&ws_rx_buffer[i], key_prefix, prefix_len) == 0)
        {
            uint16_t v = (uint16_t)(i + prefix_len);
            while ((v < header_end) && ((ws_rx_buffer[v] == ' ') || (ws_rx_buffer[v] == '\t')))
            {
                v++;
            }

            uint16_t e = v;
            while ((e < header_end) && (ws_rx_buffer[e] != '\r') && (ws_rx_buffer[e] != '\n'))
            {
                e++;
            }
            while ((e > v) && ((ws_rx_buffer[e - 1U] == ' ') || (ws_rx_buffer[e - 1U] == '\t')))
            {
                e--;
            }

            uint16_t key_len = (uint16_t)(e - v);
            if ((key_len > 0U) && (key_len < sizeof(ws_client_key)))
            {
                memcpy(ws_client_key, &ws_rx_buffer[v], key_len);
                ws_client_key[key_len] = '\0';
                key_found = true;
            }
            break;
        }
    }

    if (!key_found)
    {
        eth_ws_close_client();
        return -1;
    }

    if (eth_ws_build_accept_key(ws_client_key, ws_accept_key, sizeof(ws_accept_key)) != 0)
    {
        eth_ws_close_client();
        return -1;
    }

    size_t response_len = strlen(response_prefix) + strlen(ws_accept_key) + strlen(response_suffix);
    if ((uint32_t)tcp_sndbuf(tpcb) < (uint32_t)response_len)
    {
        return 0;
    }

    if (tcp_write(tpcb, response_prefix, strlen(response_prefix), TCP_WRITE_FLAG_COPY) != ERR_OK)
    {
        eth_ws_close_client();
        return -1;
    }
    if (tcp_write(tpcb, ws_accept_key, strlen(ws_accept_key), TCP_WRITE_FLAG_COPY) != ERR_OK)
    {
        eth_ws_close_client();
        return -1;
    }
    if (tcp_write(tpcb, response_suffix, strlen(response_suffix), TCP_WRITE_FLAG_COPY) != ERR_OK)
    {
        eth_ws_close_client();
        return -1;
    }

    (void)tcp_output(tpcb);
    ws_state = ETH_WS_STATE_OPEN;

    return (int)(header_end + 4U);
}

static int eth_ws_send_frame(uint8_t opcode, const uint8_t *data, uint16_t len)
{
    if ((ws_client_pcb == NULL) || (ws_state != ETH_WS_STATE_OPEN))
    {
        return -1;
    }

    uint8_t header[4];
    uint16_t header_len = 0U;

    header[0] = (uint8_t)(0x80U | (opcode & 0x0FU));
    if (len <= 125U)
    {
        header[1] = (uint8_t)len;
        header_len = 2U;
    }
    else
    {
        header[1] = 126U;
        header[2] = (uint8_t)((len >> 8) & 0xFFU);
        header[3] = (uint8_t)(len & 0xFFU);
        header_len = 4U;
    }

    if (tcp_sndbuf(ws_client_pcb) < (uint16_t)(header_len + len))
    {
        return -1;
    }

    if (tcp_write(ws_client_pcb, header, header_len, TCP_WRITE_FLAG_COPY) != ERR_OK)
    {
        return -1;
    }

    if ((len > 0U) && (data != NULL))
    {
        if (tcp_write(ws_client_pcb, data, len, TCP_WRITE_FLAG_COPY) != ERR_OK)
        {
            return -1;
        }
    }

    if (tcp_output(ws_client_pcb) != ERR_OK)
    {
        return -1;
    }

    return 0;
}

static int eth_ws_handle_frame(void)
{
    if (ws_rx_len < 2U)
    {
        return 0;
    }

    uint8_t *b = ws_rx_buffer;
    uint8_t opcode = (uint8_t)(b[0] & 0x0FU);
    uint8_t masked = (uint8_t)(b[1] & 0x80U);
    uint64_t payload_len = (uint64_t)(b[1] & 0x7FU);
    uint16_t offset = 2U;

    if (payload_len == 126U)
    {
        if (ws_rx_len < 4U)
        {
            return 0;
        }
        payload_len = ((uint64_t)b[2] << 8) | b[3];
        offset = 4U;
    }
    else if (payload_len == 127U)
    {
        if (ws_rx_len < 10U)
        {
            return 0;
        }

        payload_len = 0U;
        for (uint16_t i = 0U; i < 8U; i++)
        {
            payload_len = (payload_len << 8) | b[2U + i];
        }
        offset = 10U;
        if (payload_len > 65535U)
        {
            eth_ws_close_client();
            return -1;
        }
    }

    if (masked == 0U)
    {
        eth_ws_close_client();
        return -1;
    }

    uint16_t frame_len = (uint16_t)(offset + 4U + payload_len);
    if ((payload_len > ETH_WS_RX_BUFFER_SIZE) || (frame_len > ws_rx_len))
    {
        if (frame_len > ws_rx_len)
        {
            return 0;
        }
        eth_ws_close_client();
        return -1;
    }

    uint8_t *mask = &b[offset];
    uint8_t *payload = &b[offset + 4U];
    for (uint16_t i = 0U; i < (uint16_t)payload_len; i++)
    {
        payload[i] ^= mask[i & 0x3U];
    }

    if (opcode == WS_OPCODE_TEXT)
    {
        if (ws_recv_callback != NULL)
        {
            ws_recv_callback(payload, (uint16_t)payload_len, true);
        }
    }
    else if (opcode == WS_OPCODE_BINARY)
    {
        if (ws_recv_callback != NULL)
        {
            ws_recv_callback(payload, (uint16_t)payload_len, false);
        }
    }
    else if (opcode == WS_OPCODE_PING)
    {
        (void)eth_ws_send_frame(WS_OPCODE_PONG, payload, (uint16_t)payload_len);
    }
    else if (opcode == WS_OPCODE_CLOSE)
    {
        (void)eth_ws_send_frame(WS_OPCODE_CLOSE, NULL, 0U);
        eth_ws_close_client();
        return -1;
    }

    return frame_len;
}

/***************************************************************
** MARK: LwIP TCP CALLBACKS
***************************************************************/

static err_t eth_ws_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    (void)arg;
    if ((err != ERR_OK) || (newpcb == NULL))
    {
        return ERR_VAL;
    }

    if (ws_client_pcb != NULL)
    {
        tcp_abort(newpcb);
        return ERR_ABRT;
    }

    ws_client_pcb = newpcb;
    ws_state = ETH_WS_STATE_HANDSHAKE;
    ws_rx_len = 0U;

    tcp_arg(newpcb, NULL);
    tcp_recv(newpcb, eth_ws_recv_cb);
    tcp_poll(newpcb, eth_ws_poll_cb, 4U);
    tcp_err(newpcb, eth_ws_err_cb);

    return ERR_OK;
}

static err_t eth_ws_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    (void)arg;

    if ((err != ERR_OK) || (tpcb != ws_client_pcb))
    {
        if (p != NULL)
        {
            pbuf_free(p);
        }
        eth_ws_close_client();
        return ERR_OK;
    }

    if (p == NULL)
    {
        eth_ws_close_client();
        return ERR_OK;
    }

    tcp_recved(tpcb, p->tot_len);

    for (struct pbuf *q = p; q != NULL; q = q->next)
    {
        uint16_t space = (uint16_t)(ETH_WS_RX_BUFFER_SIZE - ws_rx_len);
        if (q->len > space)
        {
            pbuf_free(p);
            eth_ws_close_client();
            return ERR_OK;
        }

        memcpy(&ws_rx_buffer[ws_rx_len], q->payload, q->len);
        ws_rx_len = (uint16_t)(ws_rx_len + q->len);
    }
    pbuf_free(p);

    while ((ws_client_pcb != NULL) && (ws_rx_len > 0U))
    {
        int consumed = 0;
        if (ws_state == ETH_WS_STATE_HANDSHAKE)
        {
            consumed = eth_ws_handle_handshake(tpcb);
        }
        else if (ws_state == ETH_WS_STATE_OPEN)
        {
            consumed = eth_ws_handle_frame();
        }

        if (consumed > 0)
        {
            eth_ws_consume_rx((uint16_t)consumed);
            continue;
        }
        break;
    }

    return ERR_OK;
}

static err_t eth_ws_poll_cb(void *arg, struct tcp_pcb *tpcb)
{
    (void)arg;
    if (tpcb != ws_client_pcb)
    {
        return ERR_OK;
    }

    return ERR_OK;
}

static void eth_ws_err_cb(void *arg, err_t err)
{
    (void)arg;
    (void)err;
    ws_client_pcb = NULL;
    eth_ws_reset_client_state();
}

/***************************************************************
** MARK: PUBLIC API
***************************************************************/

int eth_ws_init(uint16_t port, eth_ws_recv_callback_t recv_callback)
{
    ws_recv_callback = recv_callback;
    ws_listen_port = (port == 0U) ? ETH_WS_DEFAULT_PORT : port;

    if (ws_listener_pcb != NULL)
    {
        return 0;
    }

    ws_listener_pcb = tcp_new();
    if (ws_listener_pcb == NULL)
    {
        return -1;
    }

    if (tcp_bind(ws_listener_pcb, IP_ADDR_ANY, ws_listen_port) != ERR_OK)
    {
        tcp_abort(ws_listener_pcb);
        ws_listener_pcb = NULL;
        return -1;
    }

    struct tcp_pcb *listen_pcb = tcp_listen_with_backlog(ws_listener_pcb, 1U);
    if (listen_pcb == NULL)
    {
        tcp_abort(ws_listener_pcb);
        ws_listener_pcb = NULL;
        return -1;
    }

    ws_listener_pcb = listen_pcb;
    tcp_arg(ws_listener_pcb, NULL);
    tcp_accept(ws_listener_pcb, eth_ws_accept_cb);

    return 0;
}

bool eth_ws_is_connected(void)
{
    return (ws_client_pcb != NULL) && (ws_state == ETH_WS_STATE_OPEN);
}

int eth_ws_send_text(const uint8_t *data, uint16_t len)
{
    return eth_ws_send_frame(WS_OPCODE_TEXT, data, len);
}

int eth_ws_send_binary(const uint8_t *data, uint16_t len)
{
    return eth_ws_send_frame(WS_OPCODE_BINARY, data, len);
}
 
