#define CORE_CM7
#include "stm32h745xx.h"
#include "uart.h"

void UART3_Init(void)
{
    /* Enable clocks */
    RCC->AHB4ENR  |= RCC_AHB4ENR_GPIODEN;   /* GPIOD for PD8/PD9 */
    RCC->APB1LENR |= RCC_APB1LENR_USART3EN;
    __DSB();

    /* PD8 = TX, PD9 = RX, both AF7 */
    /* Mode: alternate function (10) */
    GPIOD->MODER &= ~((3U << 16U) | (3U << 18U));
    GPIOD->MODER |=  ((2U << 16U) | (2U << 18U));

    /* Speed: high (10) */
    GPIOD->OSPEEDR &= ~((3U << 16U) | (3U << 18U));
    GPIOD->OSPEEDR |=  ((2U << 16U) | (2U << 18U));

    /* AF7 for PD8 and PD9 */
    GPIOD->AFR[1] &= ~((0xFU << 0U) | (0xFU << 4U));
    GPIOD->AFR[1] |=  ((7U   << 0U) | (7U   << 4U));

    /* Configure USART3:
     * APB1 clock = 120MHz
     * BRR = clock / baud = 120000000 / 115200 = 1041 (0x411)
     */
    USART3->BRR = 555U;

    /* Enable USART, TX, RX */
    USART3->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

/* Blocking single byte TX - only used by _write() */
void UART3_SendChar(char c)
{
    while (!(USART3->ISR & USART_ISR_TXE_TXFNF));
    USART3->TDR = (uint8_t)c;
}