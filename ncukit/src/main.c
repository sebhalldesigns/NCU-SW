#include <stdint.h>

#define STM32H745xx
#include <stm32h7xx.h>
#include <icc.h>


volatile uint8_t led_state = 0;

/* Model entry point functions */
extern void app_initialize(void);
extern void app_step(void);
extern void app_terminate(void);

static inline void mark_app_active(void)
{
    HSEM->R[ICC_M4_ACTIVITY_HSEM_ID] = HSEM_R_LOCK | ICC_M4_ACTIVITY_PROCESS_ID;
    (void)HSEM->R[ICC_M4_ACTIVITY_HSEM_ID];
}

static inline void mark_app_inactive(void)
{
    HSEM->R[ICC_M4_ACTIVITY_HSEM_ID] = ICC_M4_ACTIVITY_PROCESS_ID;
}

void TIM3_IRQHandler(void)
{
    if (TIM3->SR & TIM_SR_UIF)        /* check update interrupt flag */
    {
        TIM3->SR &= ~TIM_SR_UIF;      /* clear the flag or it will re-trigger immediately */

        mark_app_active();

        app_step();

        mark_app_inactive();
    }
}

int main(void)
{

    RCC->APB1LENR |= RCC_APB1LENR_TIM3EN; /* enable TIM3 clock */
    RCC->AHB4ENR |= RCC_AHB4ENR_HSEMEN;   /* enable HSEM clock */
    
    __DSB(); /* ensure that the clock is enabled before accessing GPIO registers */

    app_initialize();
    mark_app_inactive();

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
