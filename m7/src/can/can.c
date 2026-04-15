/***************************************************************
**
** NCU Source File
**
** File         :  can.c
** Module       :  can
** Author       :  Codex
** Created      :  2026-04-15 (YYYY-MM-DD)
** License      :  MIT
** Description  :  M7 FDCAN test interface
**
***************************************************************/

/***************************************************************
** MARK: INCLUDES
***************************************************************/

#include "can.h"

#include <sys/sys.h>

/***************************************************************
** MARK: CONSTANTS & MACROS
***************************************************************/

#define FDCAN_TEST_PERIOD_US          1000000U
#define FDCAN_NOMINAL_PRESCALER       10U
#define FDCAN_NOMINAL_TSEG1           13U
#define FDCAN_NOMINAL_TSEG2           2U
#define FDCAN_NOMINAL_SJW             2U
#define FDCAN_MSG_RAM_WORDS_PER_RX    4U
#define FDCAN_MSG_RAM_WORDS_PER_TX    4U
#define FDCAN1_RX_WORD_OFFSET         0U
#define FDCAN1_TX_WORD_OFFSET         (FDCAN1_RX_WORD_OFFSET + FDCAN_MSG_RAM_WORDS_PER_RX)
#define FDCAN2_RX_WORD_OFFSET         (FDCAN1_TX_WORD_OFFSET + FDCAN_MSG_RAM_WORDS_PER_TX)
#define FDCAN2_TX_WORD_OFFSET         (FDCAN2_RX_WORD_OFFSET + FDCAN_MSG_RAM_WORDS_PER_RX)
#define FDCAN_MSG_RAM_USED_WORDS      (FDCAN2_TX_WORD_OFFSET + FDCAN_MSG_RAM_WORDS_PER_TX)
#define FDCAN_TIMEOUT_LOOPS           1000000U
#define FDCAN_TX_BUFFER_INDEX         0U

/***************************************************************
** MARK: TYPEDEFS
***************************************************************/

typedef struct
{
    FDCAN_GlobalTypeDef *instance;
    uint32_t rx_word_offset;
    uint32_t tx_word_offset;
    uint32_t standard_id;
    uint8_t payload[8];
} fdcan_node_t;

/***************************************************************
** MARK: STATIC VARIABLES
***************************************************************/

static fdcan_node_t fdcan_nodes[2] = {
    { FDCAN1, FDCAN1_RX_WORD_OFFSET, FDCAN1_TX_WORD_OFFSET, 0x101U, { 'M', '7', 'C', 'A', 'N', '1', 0x01U, 0x55U } },
    { FDCAN2, FDCAN2_RX_WORD_OFFSET, FDCAN2_TX_WORD_OFFSET, 0x201U, { 'M', '7', 'C', 'A', 'N', '2', 0x02U, 0xAAU } }
};

static bool can_ready = false;
static uint32_t last_test_tx_us = 0U;

/***************************************************************
** MARK: STATIC FUNCTION DEFS
***************************************************************/

static void fdcan_enable_kernel_clock(void);
static void fdcan_configure_gpio(void);
static bool fdcan_wait_for_bits(volatile uint32_t *reg, uint32_t mask, uint32_t expected);
static void fdcan_clear_message_ram(void);
static bool fdcan_init_node(const fdcan_node_t *node);
static void fdcan_drain_rx_fifo0(const fdcan_node_t *node);
static bool fdcan_send_frame(const fdcan_node_t *node);

/***************************************************************
** MARK: GLOBAL FUNCTIONS
***************************************************************/

bool can_init(void)
{
    fdcan_enable_kernel_clock();
    fdcan_configure_gpio();
    fdcan_clear_message_ram();

    can_ready = fdcan_init_node(&fdcan_nodes[0]) && fdcan_init_node(&fdcan_nodes[1]);
    if (!can_ready)
    {
        return false;
    }

    /* Kick out one frame on each controller immediately after both are live. */
    (void)fdcan_send_frame(&fdcan_nodes[0]);
    (void)fdcan_send_frame(&fdcan_nodes[1]);
    last_test_tx_us = 0U;

    return true;
}

void can_poll(uint32_t time_us)
{
    if (!can_ready)
    {
        return;
    }

    if ((uint32_t)(time_us - last_test_tx_us) < FDCAN_TEST_PERIOD_US)
    {
        fdcan_drain_rx_fifo0(&fdcan_nodes[0]);
        fdcan_drain_rx_fifo0(&fdcan_nodes[1]);
        return;
    }

    last_test_tx_us = time_us;
    fdcan_drain_rx_fifo0(&fdcan_nodes[0]);
    fdcan_drain_rx_fifo0(&fdcan_nodes[1]);
    (void)fdcan_send_frame(&fdcan_nodes[0]);
    (void)fdcan_send_frame(&fdcan_nodes[1]);
}

/***************************************************************
** MARK: STATIC FUNCTIONS
***************************************************************/

static void fdcan_enable_kernel_clock(void)
{
    /* sys_init() already programs PLL2Q to 80 MHz; select it as the FDCAN source here. */
    RCC->D2CCIP1R = (RCC->D2CCIP1R & ~RCC_D2CCIP1R_FDCANSEL) | RCC_D2CCIP1R_FDCANSEL_1;
    RCC->APB1HENR |= RCC_APB1HENR_FDCANEN;
    (void)RCC->APB1HENR;
}

static void fdcan_configure_gpio(void)
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

static bool fdcan_wait_for_bits(volatile uint32_t *reg, uint32_t mask, uint32_t expected)
{
    uint32_t timeout = FDCAN_TIMEOUT_LOOPS;
    while (((*reg) & mask) != expected)
    {
        if (timeout-- == 0U)
        {
            return false;
        }
    }

    return true;
}

static void fdcan_clear_message_ram(void)
{
    volatile uint32_t *const msg_ram = (volatile uint32_t *)SRAMCAN_BASE;
    for (uint32_t i = 0U; i < FDCAN_MSG_RAM_USED_WORDS; i++)
    {
        msg_ram[i] = 0U;
    }
}

static bool fdcan_init_node(const fdcan_node_t *node)
{
    FDCAN_GlobalTypeDef *const fdcan = node->instance;

    fdcan->CCCR |= FDCAN_CCCR_INIT;
    if (!fdcan_wait_for_bits(&fdcan->CCCR, FDCAN_CCCR_INIT, FDCAN_CCCR_INIT))
    {
        return false;
    }

    fdcan->CCCR |= FDCAN_CCCR_CCE;

    /* Classical CAN at 500 kbit/s from 80 MHz:
     * tq = 80 MHz / 10 = 8 MHz
     * bit time = 16 tq => 500 kbit/s
     * sample point = 14/16 = 87.5%
     */
    fdcan->NBTP = ((FDCAN_NOMINAL_TSEG2 - 1U) << FDCAN_NBTP_NTSEG2_Pos)
                | ((FDCAN_NOMINAL_TSEG1 - 1U) << FDCAN_NBTP_NTSEG1_Pos)
                | ((FDCAN_NOMINAL_PRESCALER - 1U) << FDCAN_NBTP_NBRP_Pos)
                | ((FDCAN_NOMINAL_SJW - 1U) << FDCAN_NBTP_NSJW_Pos);

    fdcan->CCCR &= ~(FDCAN_CCCR_FDOE | FDCAN_CCCR_BRSE | FDCAN_CCCR_TEST | FDCAN_CCCR_MON);
    fdcan->IR = 0xFFFFFFFFUL;
    fdcan->IE = 0U;
    fdcan->ILS = 0U;
    fdcan->ILE = 0U;

    fdcan->GFC = FDCAN_GFC_RRFS | FDCAN_GFC_RRFE;
    fdcan->SIDFC = 0U;
    fdcan->XIDFC = 0U;
    fdcan->RXF0C = (node->rx_word_offset << FDCAN_RXF0C_F0SA_Pos)
                 | (1U << FDCAN_RXF0C_F0S_Pos);
    fdcan->RXF1C = 0U;
    fdcan->RXBC = 0U;
    fdcan->RXESC = 0U;
    fdcan->TXEFC = 0U;
    fdcan->TXESC = 0U; /* 8-byte payload */
    fdcan->TXBC = (node->tx_word_offset << FDCAN_TXBC_TBSA_Pos)
                | (1U << FDCAN_TXBC_NDTB_Pos);

    fdcan->CCCR &= ~(FDCAN_CCCR_CCE | FDCAN_CCCR_INIT);
    if (!fdcan_wait_for_bits(&fdcan->CCCR, FDCAN_CCCR_INIT, 0U))
    {
        return false;
    }

    return true;
}

static void fdcan_drain_rx_fifo0(const fdcan_node_t *node)
{
    FDCAN_GlobalTypeDef *const fdcan = node->instance;

    while ((fdcan->RXF0S & FDCAN_RXF0S_F0FL) != 0U)
    {
        uint32_t get_index = (fdcan->RXF0S & FDCAN_RXF0S_F0GI) >> FDCAN_RXF0S_F0GI_Pos;
        fdcan->RXF0A = (get_index << FDCAN_RXF0A_F0AI_Pos);
    }
}

static bool fdcan_send_frame(const fdcan_node_t *node)
{
    FDCAN_GlobalTypeDef *const fdcan = node->instance;

    if ((fdcan->TXBRP & (1UL << FDCAN_TX_BUFFER_INDEX)) != 0U)
    {
        return false;
    }

    volatile uint32_t *const element = ((volatile uint32_t *)SRAMCAN_BASE) + node->tx_word_offset;
    volatile uint8_t *const payload = (volatile uint8_t *)&element[2];

    element[0] = (node->standard_id & 0x7FFUL) << 18;
    element[1] = (8UL << 16);
    element[2] = 0U;
    element[3] = 0U;

    for (uint32_t i = 0U; i < 8U; i++)
    {
        payload[i] = node->payload[i];
    }

    __DMB();
    fdcan->TXBAR = (1UL << FDCAN_TX_BUFFER_INDEX);
    return true;
}
