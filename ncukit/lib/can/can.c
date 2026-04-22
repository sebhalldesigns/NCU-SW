#include "can.h"

#include <stddef.h>

#ifdef MATLAB_MEX_FILE
/* Simulink run and check */

void can_set_bitrate(uint8_t bus, uint8_t bitrate)
{
    (void)bus;
    (void)bitrate;
}

void can_transmit(uint8_t bus, const CAN_MESSAGE_BUS *message)
{
    (void)bus;
    (void)message;
}

CAN_MESSAGE_BUS can_receive(uint8_t bus, uint8_t *status, can_rx_count_t *rx_count)
{
    return can_receive_message(bus, status, rx_count);
}

CAN_MESSAGE_BUS can_receive_message(uint8_t bus, uint8_t *status, can_rx_count_t *rx_count)
{
    CAN_MESSAGE_BUS message = {0};
    (void)bus;

    if (status != NULL) {
        *status = (uint8_t)CAN_RX_STATUS_EMPTY;
    }

    if (rx_count != NULL) {
        *rx_count = 0U;
    }

    return message;
}

void can1_set_bitrate(can_bitrate_t bitrate)
{
    (void)bitrate;
}

void can2_set_bitrate(can_bitrate_t bitrate)
{
    (void)bitrate;
}

void can1_transmit(CAN_MESSAGE_BUS message)
{
    (void)message;
}

void can2_transmit(CAN_MESSAGE_BUS message)
{
    (void)message;
}

CAN_MESSAGE_BUS can1_receive(uint8_t *status, can_rx_count_t *rx_count)
{
    return can_receive_message(CAN_BUS_1, status, rx_count);
}

CAN_MESSAGE_BUS can2_receive(uint8_t *status, can_rx_count_t *rx_count)
{
    return can_receive_message(CAN_BUS_2, status, rx_count);
}

#else

/* Embedded target build */
#define STM32H745xx
#include <stm32h7xx.h>

#include <string.h>

#define FDCAN_CLOCK_HZ                80000000U
#define FDCAN_TIME_QUANTA_PER_BIT     16U
#define FDCAN_NOMINAL_TSEG1           13U
#define FDCAN_NOMINAL_TSEG2           2U
#define FDCAN_NOMINAL_SJW             2U
#define FDCAN_TIMEOUT_LOOPS           1000000U
#define FDCAN_TX_BUFFER_INDEX         0U
#define FDCAN_INIT_RETRY_INTERVAL     1000U

#define FDCAN_MSG_RAM_WORDS_PER_RX    4U
#define FDCAN_MSG_RAM_WORDS_PER_TX    4U
#define FDCAN_RX_FIFO0_ELEMENTS       1U
#define FDCAN1_RX_WORD_OFFSET         0U
#define FDCAN1_TX_WORD_OFFSET         (FDCAN1_RX_WORD_OFFSET + FDCAN_MSG_RAM_WORDS_PER_RX)
#define FDCAN2_RX_WORD_OFFSET         (FDCAN1_TX_WORD_OFFSET + FDCAN_MSG_RAM_WORDS_PER_TX)
#define FDCAN2_TX_WORD_OFFSET         (FDCAN2_RX_WORD_OFFSET + FDCAN_MSG_RAM_WORDS_PER_RX)

#define CAN_ELEMENT_XTD               (1UL << 30U)
#define CAN_ELEMENT_RTR               (1UL << 29U)
#define CAN_ELEMENT_STD_ID_POS        18U

#define CAN_ELEMENT_DLC_POS           16U
#define CAN_ELEMENT_TIMESTAMP_MASK    0xFFFFUL

typedef struct {
    FDCAN_GlobalTypeDef *instance;
    uint32_t rx_word_offset;
    uint32_t tx_word_offset;
    uint8_t bitrate;
    bool initialized;
    uint32_t init_retry_count;
    uint32_t rx_count;
} can_node_t;

static can_node_t can_nodes[2] = {
    { FDCAN1, FDCAN1_RX_WORD_OFFSET, FDCAN1_TX_WORD_OFFSET, CAN1_DEFAULT_BITRATE, false, 0U, 0U },
    { FDCAN2, FDCAN2_RX_WORD_OFFSET, FDCAN2_TX_WORD_OFFSET, CAN2_DEFAULT_BITRATE, false, 0U, 0U }
};

static bool can_common_ready = false;

static can_node_t *can_get_node(uint8_t bus);
static bool can_is_valid_bitrate(uint8_t bitrate);
static uint32_t can_bitrate_to_brp(uint8_t bitrate);
static uint8_t can_length_to_dlc(uint8_t length);
static uint8_t can_dlc_to_length(uint8_t dlc);
static bool can_configure_pll2_for_fdcan(void);
static bool can_enable_kernel_clock(void);
static void can_configure_gpio(void);
static bool can_wait_for_bits(volatile uint32_t *reg, uint32_t mask, uint32_t expected);
static void can_clear_node_message_ram(const can_node_t *node);
static bool can_init_node(can_node_t *node);
static bool can_ensure_ready(can_node_t *node);

void can_set_bitrate(uint8_t bus, uint8_t bitrate)
{
    can_node_t *node = can_get_node(bus);
    if ((node == NULL) || !can_is_valid_bitrate(bitrate)) return;

    if (node->bitrate != bitrate) {
        node->bitrate = bitrate;
        node->initialized = false;
        node->init_retry_count = 0U;
    }

    /* Initialize from configuration context so ISR-step paths stay lightweight. */
    (void)can_ensure_ready(node);
}

void can_transmit(uint8_t bus, const CAN_MESSAGE_BUS *message)
{
    can_node_t *node = can_get_node(bus);
    uint8_t length;
    uint8_t dlc;
    uint32_t r0;
    uint32_t r1;
    volatile uint32_t *element;
    uint32_t dataWord0;
    uint32_t dataWord1;
    uint32_t i;

    if ((node == NULL) || (message == NULL)) return;
    if (!node->initialized) return;
    if ((node->instance->TXBRP & (1UL << FDCAN_TX_BUFFER_INDEX)) != 0U) return;

    length = message->Length;
    if (length > 8U) length = 8U;
    dlc = can_length_to_dlc(length);

    r0 = 0U;
    if (message->Extended != 0U) {
        r0 |= CAN_ELEMENT_XTD;
        r0 |= (message->ID & 0x1FFFFFFFUL);
    } else {
        r0 |= ((message->ID & 0x7FFUL) << CAN_ELEMENT_STD_ID_POS);
    }

    if (message->Remote != 0U) {
        r0 |= CAN_ELEMENT_RTR;
    }

    r1 = ((uint32_t)dlc << CAN_ELEMENT_DLC_POS);

    element = ((volatile uint32_t *)SRAMCAN_BASE) + node->tx_word_offset;

    element[0] = r0;
    element[1] = r1;
    dataWord0 = 0U;
    dataWord1 = 0U;

    if (message->Remote == 0U) {
        for (i = 0U; i < length; i++) {
            if (i < 4U) {
                dataWord0 |= ((uint32_t)message->Data[i]) << (8U * i);
            } else {
                dataWord1 |= ((uint32_t)message->Data[i]) << (8U * (i - 4U));
            }
        }
    }
    element[2] = dataWord0;
    element[3] = dataWord1;

    __DMB();
    node->instance->TXBAR = (1UL << FDCAN_TX_BUFFER_INDEX);
}

CAN_MESSAGE_BUS can_receive(uint8_t bus, uint8_t *status, can_rx_count_t *rx_count)
{
    return can_receive_message(bus, status, rx_count);
}

CAN_MESSAGE_BUS can_receive_message(uint8_t bus, uint8_t *status, can_rx_count_t *rx_count)
{
    can_node_t *node = can_get_node(bus);
    static CAN_MESSAGE_BUS message;
    FDCAN_GlobalTypeDef *fdcan;
    uint32_t get_index;
    volatile uint32_t *element;
    uint32_t r0;
    uint32_t r1;
    uint32_t dataWord0;
    uint32_t dataWord1;
    uint8_t dlc;
    uint8_t length;
    uint32_t i;

    if (status != NULL) {
        *status = (uint8_t)CAN_RX_STATUS_EMPTY;
    }
    (void)memset(&message, 0, sizeof(message));

    if (node == NULL) {
        if (status != NULL) {
            *status = (uint8_t)CAN_RX_STATUS_INVALID_BUS;
        }
        if (rx_count != NULL) {
            *rx_count = 0U;
        }
        return message;
    }

    if (!node->initialized) {
        if (status != NULL) {
            *status = (uint8_t)CAN_RX_STATUS_NOT_READY;
        }
        if (rx_count != NULL) {
            *rx_count = node->rx_count;
        }
        return message;
    }

    fdcan = node->instance;
    if ((fdcan->RXF0S & FDCAN_RXF0S_F0FL) == 0U) {
        if (rx_count != NULL) {
            *rx_count = node->rx_count;
        }
        return message;
    }

    get_index = (fdcan->RXF0S & FDCAN_RXF0S_F0GI) >> FDCAN_RXF0S_F0GI_Pos;
    if (get_index >= FDCAN_RX_FIFO0_ELEMENTS) {
        /* Defensive guard: FIFO0 configured for one element, valid index is only 0. */
        fdcan->RXF0A = 0U;
        if (status != NULL) {
            *status = (uint8_t)CAN_RX_STATUS_NOT_READY;
        }
        return message;
    }

    element = ((volatile uint32_t *)SRAMCAN_BASE) + node->rx_word_offset + (get_index * FDCAN_MSG_RAM_WORDS_PER_RX);
    r0 = element[0];
    r1 = element[1];
    dataWord0 = element[2];
    dataWord1 = element[3];

    message.Extended = ((r0 & CAN_ELEMENT_XTD) != 0U) ? 1U : 0U;
    message.Remote = ((r0 & CAN_ELEMENT_RTR) != 0U) ? 1U : 0U;
    message.Error = 0U;
    message.Timestamp = 0.0;

    if (message.Extended != 0U) {
        message.ID = (r0 & 0x1FFFFFFFUL);
    } else {
        message.ID = ((r0 >> CAN_ELEMENT_STD_ID_POS) & 0x7FFUL);
    }

    dlc = (uint8_t)((r1 >> CAN_ELEMENT_DLC_POS) & 0x0FU);
    length = can_dlc_to_length(dlc);
    if (length > 8U) {
        length = 8U;
    }
    message.Length = length;

    if (message.Remote == 0U) {
        for (i = 0U; i < length; i++) {
            if (i < 4U) {
                message.Data[i] = (uint8_t)(dataWord0 >> (8U * i));
            } else {
                message.Data[i] = (uint8_t)(dataWord1 >> (8U * (i - 4U)));
            }
        }
    }

    fdcan->RXF0A = (get_index << FDCAN_RXF0A_F0AI_Pos);

    node->rx_count++;

    if (status != NULL) {
        *status = (uint8_t)CAN_RX_STATUS_OK;
    }
    if (rx_count != NULL) {
        *rx_count = node->rx_count;
    }

    return message;
}

void can1_set_bitrate(can_bitrate_t bitrate)
{
    can_set_bitrate(CAN_BUS_1, bitrate);
}

void can2_set_bitrate(can_bitrate_t bitrate)
{
    can_set_bitrate(CAN_BUS_2, bitrate);
}

void can1_transmit(CAN_MESSAGE_BUS message)
{
    can_transmit(CAN_BUS_1, &message);
}

void can2_transmit(CAN_MESSAGE_BUS message)
{
    can_transmit(CAN_BUS_2, &message);
}

CAN_MESSAGE_BUS can1_receive(uint8_t *status, can_rx_count_t *rx_count)
{
    return can_receive_message(CAN_BUS_1, status, rx_count);
}

CAN_MESSAGE_BUS can2_receive(uint8_t *status, can_rx_count_t *rx_count)
{
    return can_receive_message(CAN_BUS_2, status, rx_count);
}

static can_node_t *can_get_node(uint8_t bus)
{
    switch (bus) {
        case CAN_BUS_1: return &can_nodes[0];
        case CAN_BUS_2: return &can_nodes[1];
        default: return NULL;
    }
}

static bool can_is_valid_bitrate(uint8_t bitrate)
{
    switch ((can_bitrate_t)bitrate) {
        case CAN_BITRATE_125K:
        case CAN_BITRATE_250K:
        case CAN_BITRATE_500K:
        case CAN_BITRATE_1M:
            return true;
        default:
            return false;
    }
}

static uint32_t can_bitrate_to_brp(uint8_t bitrate)
{
    switch ((can_bitrate_t)bitrate) {
        case CAN_BITRATE_125K:
            return (FDCAN_CLOCK_HZ / (125000U * FDCAN_TIME_QUANTA_PER_BIT));
        case CAN_BITRATE_250K:
            return (FDCAN_CLOCK_HZ / (250000U * FDCAN_TIME_QUANTA_PER_BIT));
        case CAN_BITRATE_500K:
            return (FDCAN_CLOCK_HZ / (500000U * FDCAN_TIME_QUANTA_PER_BIT));
        case CAN_BITRATE_1M:
            return (FDCAN_CLOCK_HZ / (1000000U * FDCAN_TIME_QUANTA_PER_BIT));
        default:
            return 0U;
    }
}

static uint8_t can_length_to_dlc(uint8_t length)
{
    if (length <= 8U) return length;
    if (length <= 12U) return 9U;
    if (length <= 16U) return 10U;
    if (length <= 20U) return 11U;
    if (length <= 24U) return 12U;
    if (length <= 32U) return 13U;
    if (length <= 48U) return 14U;
    return 15U;
}

static uint8_t can_dlc_to_length(uint8_t dlc)
{
    static const uint8_t dlc_to_len_map[16] = {
        0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U,
        8U, 12U, 16U, 20U, 24U, 32U, 48U, 64U
    };

    return dlc_to_len_map[dlc & 0x0FU];
}

static bool can_configure_pll2_for_fdcan(void)
{
    if ((RCC->CR & RCC_CR_PLL2RDY) != 0U) {
        return true;
    }

    /* Configure PLL2Q = 80 MHz from HSI (64 MHz): 64/4*20/4 = 80 MHz. */
    RCC->PLLCKSELR = (RCC->PLLCKSELR & ~RCC_PLLCKSELR_DIVM2)
                   | (4U << RCC_PLLCKSELR_DIVM2_Pos);
    RCC->PLLCFGR = (RCC->PLLCFGR
                & ~(RCC_PLLCFGR_PLL2RGE | RCC_PLLCFGR_PLL2VCOSEL | RCC_PLLCFGR_PLL2FRACEN))
                | RCC_PLLCFGR_PLL2RGE_3
                | RCC_PLLCFGR_DIVQ2EN;
    RCC->PLL2DIVR = ((20U - 1U) << RCC_PLL2DIVR_N2_Pos)
                  | ((2U  - 1U) << RCC_PLL2DIVR_P2_Pos)
                  | ((4U  - 1U) << RCC_PLL2DIVR_Q2_Pos)
                  | ((2U  - 1U) << RCC_PLL2DIVR_R2_Pos);

    RCC->CR |= RCC_CR_PLL2ON;
    return can_wait_for_bits(&RCC->CR, RCC_CR_PLL2RDY, RCC_CR_PLL2RDY);
}

static bool can_enable_kernel_clock(void)
{
    if (!can_configure_pll2_for_fdcan()) {
        return false;
    }

    /* Select PLL2Q as FDCAN kernel clock and enable peripheral clock. */
    RCC->D2CCIP1R = (RCC->D2CCIP1R & ~RCC_D2CCIP1R_FDCANSEL) | RCC_D2CCIP1R_FDCANSEL_1;
    RCC->APB1HENR |= RCC_APB1HENR_FDCANEN;
    (void)RCC->APB1HENR;
    return true;
}

static void can_configure_gpio(void)
{
    /* FDCAN1: PD0 RX, PD1 TX (AF9)
     * FDCAN2: PB5 RX, PB6 TX (AF9)
     */
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOBEN | RCC_AHB4ENR_GPIODEN;
    (void)RCC->AHB4ENR;

    GPIOD->MODER = (GPIOD->MODER
                & ~(0x3UL << (0U * 2U) | 0x3UL << (1U * 2U)))
                |  (0x2UL << (0U * 2U) | 0x2UL << (1U * 2U));
    GPIOD->OTYPER &= ~(0x1UL << 0U | 0x1UL << 1U);
    GPIOD->OSPEEDR |= (0x3UL << (0U * 2U) | 0x3UL << (1U * 2U));
    GPIOD->PUPDR &= ~(0x3UL << (0U * 2U) | 0x3UL << (1U * 2U));
    GPIOD->AFR[0] = (GPIOD->AFR[0]
                   & ~(0xFUL << (0U * 4U) | 0xFUL << (1U * 4U)))
                   |  (9UL << (0U * 4U) | 9UL << (1U * 4U));

    GPIOB->MODER = (GPIOB->MODER
                & ~(0x3UL << (5U * 2U) | 0x3UL << (6U * 2U)))
                |  (0x2UL << (5U * 2U) | 0x2UL << (6U * 2U));
    GPIOB->OTYPER &= ~(0x1UL << 5U | 0x1UL << 6U);
    GPIOB->OSPEEDR |= (0x3UL << (5U * 2U) | 0x3UL << (6U * 2U));
    GPIOB->PUPDR &= ~(0x3UL << (5U * 2U) | 0x3UL << (6U * 2U));
    GPIOB->AFR[0] = (GPIOB->AFR[0]
                   & ~(0xFUL << (5U * 4U) | 0xFUL << (6U * 4U)))
                   |  (9UL << (5U * 4U) | 9UL << (6U * 4U));
}

static bool can_wait_for_bits(volatile uint32_t *reg, uint32_t mask, uint32_t expected)
{
    uint32_t timeout = FDCAN_TIMEOUT_LOOPS;
    while (((*reg) & mask) != expected) {
        if (timeout-- == 0U) {
            return false;
        }
    }
    return true;
}

static void can_clear_node_message_ram(const can_node_t *node)
{
    volatile uint32_t *msg_ram = (volatile uint32_t *)SRAMCAN_BASE;
    uint32_t i;

    for (i = 0U; i < FDCAN_MSG_RAM_WORDS_PER_RX; i++) {
        msg_ram[node->rx_word_offset + i] = 0U;
    }
    for (i = 0U; i < FDCAN_MSG_RAM_WORDS_PER_TX; i++) {
        msg_ram[node->tx_word_offset + i] = 0U;
    }
}

static bool can_init_node(can_node_t *node)
{
    FDCAN_GlobalTypeDef *fdcan = node->instance;
    uint32_t brp = can_bitrate_to_brp(node->bitrate);

    if (brp == 0U) return false;

    fdcan->CCCR |= FDCAN_CCCR_INIT;
    if (!can_wait_for_bits(&fdcan->CCCR, FDCAN_CCCR_INIT, FDCAN_CCCR_INIT)) return false;

    fdcan->CCCR |= FDCAN_CCCR_CCE;

    fdcan->NBTP = ((FDCAN_NOMINAL_TSEG2 - 1U) << FDCAN_NBTP_NTSEG2_Pos)
                | ((FDCAN_NOMINAL_TSEG1 - 1U) << FDCAN_NBTP_NTSEG1_Pos)
                | ((brp - 1U) << FDCAN_NBTP_NBRP_Pos)
                | ((FDCAN_NOMINAL_SJW - 1U) << FDCAN_NBTP_NSJW_Pos);

    fdcan->CCCR &= ~(FDCAN_CCCR_FDOE | FDCAN_CCCR_BRSE | FDCAN_CCCR_TEST | FDCAN_CCCR_MON);
    fdcan->IR = 0xFFFFFFFFUL;
    fdcan->IE = 0U;
    fdcan->ILS = 0U;
    fdcan->ILE = 0U;

    /* Accept non-matching standard/extended frames into Rx FIFO0. */
    fdcan->GFC = 0U;
    fdcan->SIDFC = 0U;
    fdcan->XIDFC = 0U;
    fdcan->RXF0C = (node->rx_word_offset << FDCAN_RXF0C_F0SA_Pos)
                 | (1U << FDCAN_RXF0C_F0S_Pos);
    fdcan->RXF1C = 0U;
    fdcan->RXBC = 0U;
    fdcan->RXESC = 0U; /* 8-byte Rx element size */

    fdcan->TXEFC = 0U;
    fdcan->TXESC = 0U; /* 8-byte Tx element size */
    fdcan->TXBC = (node->tx_word_offset << FDCAN_TXBC_TBSA_Pos)
                | (1U << FDCAN_TXBC_NDTB_Pos);

    fdcan->CCCR &= ~(FDCAN_CCCR_CCE | FDCAN_CCCR_INIT);
    if (!can_wait_for_bits(&fdcan->CCCR, FDCAN_CCCR_INIT, 0U)) return false;

    return true;
}

static bool can_ensure_ready(can_node_t *node)
{
    if (!can_common_ready) {
        if (!can_enable_kernel_clock()) {
            return false;
        }
        can_configure_gpio();
        can_common_ready = true;
    }

    if (node->initialized) return true;
    if (node->init_retry_count > 0U) {
        node->init_retry_count--;
        return false;
    }

    can_clear_node_message_ram(node);
    node->initialized = can_init_node(node);
    if (node->initialized) {
        node->rx_count = 0U;
        node->init_retry_count = 0U;
    } else {
        node->init_retry_count = FDCAN_INIT_RETRY_INTERVAL;
    }

    return node->initialized;
}

#endif
