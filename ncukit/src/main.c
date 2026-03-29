#include <stdint.h>

#define STM32H745xx
#include <stm32h7xx.h>

volatile uint8_t led_state = 0;

/* Model entry point functions */
extern void app_initialize(void);
extern void app_step(void);
extern void app_terminate(void);

void TIM3_IRQHandler(void)
{
    if (TIM3->SR & TIM_SR_UIF)        /* check update interrupt flag */
    {
        TIM3->SR &= ~TIM_SR_UIF;      /* clear the flag or it will re-trigger immediately */

        app_step();
    }
}

int main(void)
{

    RCC->APB1LENR |= RCC_APB1LENR_TIM3EN; /* enable TIM3 clock */
    
    __DSB(); /* ensure that the clock is enabled before accessing GPIO registers */

    app_initialize();

    volatile int wait = 0;
    
    while (wait < 1000) 
    {
        wait++;
    }

    // TIM3 setup 
    TIM3->PSC  = 199;
    TIM3->ARR  = 999;   
    TIM3->DIER |= TIM_DIER_UIE;

    NVIC_SetPriority(TIM3_IRQn, 1);
    NVIC_EnableIRQ(TIM3_IRQn);

    TIM3->CR1 |= TIM_CR1_CEN;

    while (wait < 1000) 
    {
        wait++;
    }

}