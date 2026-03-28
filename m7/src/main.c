
#include <sys/sys.h>
#include <eth/eth.h>

volatile uint8_t led_state = 0;

void TIM2_IRQHandler(void)
{
    if (TIM2->SR & TIM_SR_UIF)        /* check update interrupt flag */
    {
        TIM2->SR &= ~TIM_SR_UIF;      /* clear the flag or it will re-trigger immediately */

        led_state ^= 1;
        if (led_state)
        {
            GPIOE->BSRR = (1 << 1);           /* set PE1 high */
        }
        else
        {
            GPIOE->BSRR = (1 << (1 + 16));    /* set PE1 low */
        }    
    }
}

int main(void)
{

    sys_init();


    /* ===================== RMII GPIO CONFIG ===================== */
    /* All RMII pins: Alternate function mode (10), AF11, high speed (11), no pull (00) */

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

    /* --- PB13 (RMII_TXD1) --- */
    GPIOB->MODER  = (GPIOB->MODER  & ~(0x3 << (13*2))) | (0x2 << (13*2));
    GPIOB->OSPEEDR |= (0x3 << (13*2));
    GPIOB->PUPDR  &= ~(0x3 << (13*2));
    /* PB13 is in AFRH (pins 8-15), index = pin - 8 */
    GPIOB->AFR[1] = (GPIOB->AFR[1] & ~(0xF << ((13-8)*4))) | (11U << ((13-8)*4));

    /* --- PC1 (RMII_MDC), PC4 (RMII_RXD0), PC5 (RMII_RXD1) --- */
    GPIOC->MODER = (GPIOC->MODER
                & ~(0x3 << (1*2) | 0x3 << (4*2) | 0x3 << (5*2)))
                | (0x2 << (1*2) | 0x2 << (4*2) | 0x2 << (5*2));

    GPIOC->OSPEEDR |= (0x3 << (1*2) | 0x3 << (4*2) | 0x3 << (5*2));
    GPIOC->PUPDR   &= ~(0x3 << (1*2) | 0x3 << (4*2) | 0x3 << (5*2));

    GPIOC->AFR[0] = (GPIOC->AFR[0]
                    & ~(0xF << (1*4) | 0xF << (4*4) | 0xF << (5*4)))
                    | (11U << (1*4) | 11U << (4*4) | 11U << (5*4));

    /* --- PG11 (RMII_TX_EN), PG13 (RMII_TXD0) --- */
    GPIOG->MODER = (GPIOG->MODER
                & ~(0x3 << (11*2) | 0x3 << (13*2)))
                | (0x2 << (11*2) | 0x2 << (13*2));

    GPIOG->OSPEEDR |= (0x3 << (11*2) | 0x3 << (13*2));
    GPIOG->PUPDR   &= ~(0x3 << (11*2) | 0x3 << (13*2));

    /* PG11 and PG13 are in AFRH */
    GPIOG->AFR[1] = (GPIOG->AFR[1]
                    & ~(0xF << ((11-8)*4) | 0xF << ((13-8)*4)))
                    | (11U << ((11-8)*4) | 11U << ((13-8)*4));

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

    eth_init();

    /* ===================== TIM2 & GPIOE SETUP ===================== */

    RCC->APB1LENR |= RCC_APB1LENR_TIM2EN; /* enable TIM2 clock */
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOEEN; /* enable GPIO clock */
    
    __DSB(); /* ensure that the clock is enabled before accessing GPIO registers */

    volatile int wait = 0;
    
    while (wait < 1000) 
    {
        wait++;
    }

    /* GPIOE setup */

    GPIOE->MODER &= ~(0x3 << (1 * 2)); /* clear bits for PE1 */
    GPIOE->MODER |= (0x1 << (1 * 2)); /* set PE1 to output mode */

    GPIOE->OTYPER &= ~(0x1 << 1); /* set PE1 to push-pull */
    GPIOE->OSPEEDR &= (0x3 << (1 * 2));  /* set PE1 to slow speed */
    GPIOE->PUPDR &= ~(0x3 << (1 * 2)); /* set PE1 to no pull-up, no pull-down */

    /* TIM2 setup */
    TIM2->PSC = 50000;  /* faster tick */
    TIM2->ARR = 4000;    /* ~100ms */
    TIM2->DIER |= TIM_DIER_UIE;  /* enable update interrupt */

    /* enable TIM6 interrupt in NVIC */
    NVIC_SetPriority(TIM2_IRQn, 1);
    NVIC_EnableIRQ(TIM2_IRQn);

    TIM2->CR1  |= TIM_CR1_CEN;   /* start the timer */
    
    while (wait < 1000) 
    {
        wait++;
    }

    while (1)
    {
        eth_poll();
    }
}
