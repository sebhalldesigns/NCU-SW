
#include <sys/sys.h>


volatile uint8_t led_state = 0;

void TIM2_IRQHandler(void)
{
    if (TIM2->SR & TIM_SR_UIF)
    {
        TIM2->SR &= ~TIM_SR_UIF;

        led_state ^= 1;
        if (led_state)
        {
            GPIOE->BSRR = (1 << (15 + 16));  /* set PE15 low — LED on (active low) */
        }
        else
        {
            GPIOE->BSRR = (1 << 15);          /* set PE15 high — LED off */
        }
    }
}

int main(void)
{

    /* Enable clocks for GPIOE and TIM2 */
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOEEN;
    RCC->APB1LENR |= RCC_APB1LENR_TIM2EN;

    /* GPIOE PE15 setup */
    GPIOE->MODER  &= ~(0x3U << (15 * 2));  /* clear mode bits */
    GPIOE->MODER  |=  (0x1U << (15 * 2));  /* output mode */
    GPIOE->OTYPER &= ~(0x1U << 15);        /* push-pull */
    GPIOE->OSPEEDR &= ~(0x3U << (15 * 2)); /* low speed */
    GPIOE->PUPDR  &= ~(0x3U << (15 * 2));  /* no pull */

    /* Start with LED off (PE15 high = LED off for active low) */
    GPIOE->BSRR = (1 << 15);

    /* TIM2 setup — 64MHz HSI on reset */
    /* 64MHz / (63999+1) / (999+1) = 1Hz -> 500ms per toggle */
    TIM2->PSC  = 63999;
    TIM2->ARR  = 999;
    TIM2->DIER |= TIM_DIER_UIE;

    NVIC_SetPriority(TIM2_IRQn, 1);
    NVIC_EnableIRQ(TIM2_IRQn);

    TIM2->CR1 |= TIM_CR1_CEN;

    while (1)
    {
    }
}