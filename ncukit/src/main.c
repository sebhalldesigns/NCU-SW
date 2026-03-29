#include <stdint.h>

#define STM32H745xx
#include <stm32h7xx.h>

volatile uint8_t led_state = 0;

void TIM3_IRQHandler(void)
{
    if (TIM3->SR & TIM_SR_UIF)        /* check update interrupt flag */
    {
        TIM3->SR &= ~TIM_SR_UIF;      /* clear the flag or it will re-trigger immediately */

        led_state ^= 1;
        if (led_state)
        {
            GPIOB->BSRR = (1 << 0);           /* set PE1 high */
        }
        else
        {
            GPIOB->BSRR = (1 << (0 + 16));    /* set PE1 low */
        }    
    }
}

int main(void)
{

    RCC->APB1LENR |= RCC_APB1LENR_TIM3EN; /* enable TIM3 clock */
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOBEN; /* enable GPIO clock */
    
    __DSB(); /* ensure that the clock is enabled before accessing GPIO registers */

    volatile int wait = 0;
    
    while (wait < 1000) 
    {
        wait++;
    }

    // GPIOB - PB0
    GPIOB->MODER &= ~(0x3 << (0 * 2));
    GPIOB->MODER |=  (0x1 << (0 * 2));
    GPIOB->OTYPER  &= ~(1 << 0);
    GPIOB->OSPEEDR &= ~(0x3 << (0 * 2));
    GPIOB->PUPDR   &= ~(0x3 << (0 * 2));

    // TIM3 setup 
    TIM3->PSC  = 50000;
    TIM3->ARR  = 1000;    // different rate to M7 so you can visually distinguish
    TIM3->DIER |= TIM_DIER_UIE;

    NVIC_SetPriority(TIM3_IRQn, 1);
    NVIC_EnableIRQ(TIM3_IRQn);

    TIM3->CR1 |= TIM_CR1_CEN;

    while (wait < 1000) 
    {
        wait++;
    }

}