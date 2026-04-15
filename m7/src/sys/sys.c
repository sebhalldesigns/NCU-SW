/***************************************************************
**
** NCU Source File
**
** File         :  sys.c
** Module       :  sys
** Author       :  SH
** Created      :  2026-03-28 (YYYY-MM-DD)
** License      :  MIT
** Description  :  NCU System Interface
**
***************************************************************/

/***************************************************************
** MARK: INCLUDES
***************************************************************/

#include "sys.h"


/***************************************************************
** MARK: CONSTANTS & MACROS
***************************************************************/

/***************************************************************
** MARK: TYPEDEFS
***************************************************************/

/***************************************************************
** MARK: STATIC VARIABLES
***************************************************************/

static volatile uint32_t ms_ticks = 0;

/***************************************************************
** MARK: STATIC FUNCTION DEFS
***************************************************************/

/***************************************************************
** MARK: GLOBAL FUNCTIONS
***************************************************************/

bool sys_init()
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

    /* ===================== PLL2 FOR FDCAN ===================== */
    /* HSI /4 * 20 /4 = 80MHz on PLL2Q for deterministic 500k CAN timing. */
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
    while (!(RCC->CR & RCC_CR_PLL2RDY));

    /* ===================== ETHERNET CLOCKS ===================== */
    /* Select RMII mode - must be done before enabling ETH clock */
    SYSCFG->PMCR = (SYSCFG->PMCR & ~SYSCFG_PMCR_EPIS_SEL) | SYSCFG_PMCR_EPIS_SEL_2;

    /* Enable ETH MAC clocks */
    RCC->AHB1ENR |= RCC_AHB1ENR_ETH1MACEN
                | RCC_AHB1ENR_ETH1TXEN
                | RCC_AHB1ENR_ETH1RXEN;

    /* Enable GPIO clocks for RMII pins:
    PA1, PA2, PA7 → GPIOA
    PB13          → GPIOB
    PC1, PC4, PC5 → GPIOC
    PG11, PG13    → GPIOG */
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOAEN
                | RCC_AHB4ENR_GPIOBEN
                | RCC_AHB4ENR_GPIOCEN
                | RCC_AHB4ENR_GPIOGEN;

    (void)RCC->AHB4ENR;

    /* 1ms tick for lwIP sys_now(): 400MHz / 400000 = 1kHz */
    SysTick_Config(400000);

    RCC->APB1LENR |= RCC_APB1LENR_TIM2EN;
    (void)RCC->APB1LENR;

    
    /* TIM2 setup - system us counter */
    TIM2->CR1 = 0;
    TIM2->PSC  = 199; /* 1MHz */
    TIM2->ARR  = 0xFFFFFFFF; /* max ARR for free-running */
    /* Latch PSC/ARR immediately (otherwise PSC applies only after first update). */
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0;
    TIM2->CNT = 0;

    /* start timer */
    TIM2->CR1 |= TIM_CR1_CEN; 

    return true;
}

uint32_t sys_now()
{
    return ms_ticks;
}

uint32_t sys_micros()
{
    return TIM2->CNT;
}

void SysTick_Handler(void)
{
    ms_ticks++;
}

/***************************************************************
** MARK: STATIC FUNCTIONS
***************************************************************/

