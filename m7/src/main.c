#include <stdint.h>

#define STM32H745xx
#include <stm32h7xx.h>

#include <stdio.h>

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
    TIM2->ARR = 1000;    /* ~100ms */
    TIM2->DIER |= TIM_DIER_UIE;  /* enable update interrupt */

    /* enable TIM6 interrupt in NVIC */
    NVIC_SetPriority(TIM2_IRQn, 1);
    NVIC_EnableIRQ(TIM2_IRQn);

    TIM2->CR1  |= TIM_CR1_CEN;   /* start the timer */
    
    while (wait < 1000) 
    {
        wait++;
    }
}