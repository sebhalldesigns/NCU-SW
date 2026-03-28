#include "stm32h745xx.h"
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

    /* ===================== POWER & VOS ===================== */
    /* Dev board: SMPS only, LDOEN not set - VOS0 not possible, use VOS1 (400MHz max) */
    /* Note: System starts at VOS1 (current_vos=1), so no voltage change needed */

    /* Enable SYSCFG clock */
    RCC->APB4ENR |= RCC_APB4ENR_SYSCFGEN;
    (void)RCC->APB4ENR;

    /* ===================== FLASH LATENCY ===================== */
    /* 2 wait states for 400MHz at VOS1 */
    FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY) | FLASH_ACR_LATENCY_2WS;
    while ((FLASH->ACR & FLASH_ACR_LATENCY) != FLASH_ACR_LATENCY_2WS);

    /* ===================== PLL1 ===================== */
    /* HSI /4 * 50 /2 = 400MHz */
    RCC->PLLCKSELR = (RCC->PLLCKSELR & ~(RCC_PLLCKSELR_PLLSRC | RCC_PLLCKSELR_DIVM1))
                | RCC_PLLCKSELR_PLLSRC_HSI
                | (4U << RCC_PLLCKSELR_DIVM1_Pos);

    /* VCI range 3, wide VCO, no fractional */
    RCC->PLLCFGR = (RCC->PLLCFGR & ~(RCC_PLLCFGR_PLL1RGE | RCC_PLLCFGR_PLL1VCOSEL | RCC_PLLCFGR_PLL1FRACEN))
                | RCC_PLLCFGR_PLL1RGE_3;

    /* N=50, P=2, Q=2, R=2 → 16*50/2 = 400MHz */
    RCC->PLL1DIVR = ((50U - 1U) << RCC_PLL1DIVR_N1_Pos)
                | ((2U  - 1U) << RCC_PLL1DIVR_P1_Pos)
                | ((2U  - 1U) << RCC_PLL1DIVR_Q1_Pos)
                | ((2U  - 1U) << RCC_PLL1DIVR_R1_Pos);

    /* Enable PLL1 and wait for lock */
    RCC->CR |= RCC_CR_PLL1ON;
    while (!(RCC->CR & RCC_CR_PLL1RDY));

    /* ===================== BUS PRESCALERS ===================== */
    /* D1: SYSCLK /1, HCLK /2, APB3 /2 */
    RCC->D1CFGR = (RCC->D1CFGR & ~(RCC_D1CFGR_D1CPRE | RCC_D1CFGR_HPRE | RCC_D1CFGR_D1PPRE))
                | RCC_D1CFGR_D1CPRE_DIV1
                | RCC_D1CFGR_HPRE_DIV2
                | RCC_D1CFGR_D1PPRE_DIV2;

    /* D2: APB1 /2, APB2 /2 */
    RCC->D2CFGR = (RCC->D2CFGR & ~(RCC_D2CFGR_D2PPRE1 | RCC_D2CFGR_D2PPRE2))
                | RCC_D2CFGR_D2PPRE1_DIV2
                | RCC_D2CFGR_D2PPRE2_DIV2;

    /* D3: APB4 /2 */
    RCC->D3CFGR = (RCC->D3CFGR & ~RCC_D3CFGR_D3PPRE) | RCC_D3CFGR_D3PPRE_DIV2;

    /* ===================== SWITCH SYSCLK TO PLL1 ===================== */
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL1;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL1);


    

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