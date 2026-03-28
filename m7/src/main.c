
#include <sys/sys.h>
#include <eth/eth.h>

#define UDP_ECHO_PORT 5005U

volatile uint8_t led_state = 0;
volatile uint32_t udp_rx_packets = 0U;
volatile uint32_t udp_tx_failures = 0U;

static void udp_echo_callback(const uint8_t *data, uint16_t len, uint32_t src_ip, uint16_t src_port)
{
    udp_rx_packets++;

    if ((data == NULL) || (len == 0U))
    {
        return;
    }

    if (eth_udp_send(src_ip, src_port, data, len) != 0)
    {
        udp_tx_failures++;
    }
}

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
    eth_init();
    eth_udp_bind(UDP_ECHO_PORT, udp_echo_callback);

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

    /* Main loop: always service ethernet so DHCP/timeouts keep progressing. */
    while (1)
    {
        if (eth_packet_ready)
        {
            eth_packet_ready = 0;
        }

        eth_poll();
    }
}
