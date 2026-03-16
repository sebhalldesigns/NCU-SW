#include <stdint.h>

#define STM32H745xx
#define CORE_CM7
#include <stm32h7xx.h>

void delay(void) {
    for (volatile uint32_t i = 0; i < 10000000; i++);
}

int main(void)
{
    /* enable GPIO clock */
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOEEN; 
    //__DSB(); /* ensure that the clock is enabled before accessing GPIO registers */

    volatile int wait = 0;
    
    while (wait < 1000) 
    {
        wait++;
    }

    GPIOE->MODER &= ~(0x3 << (1 * 2)); /* clear bits for PE1 */
    GPIOE->MODER |= (0x1 << (1 * 2)); /* set PE1 to output mode */

    GPIOE->OTYPER &= ~(0x1 << 1); /* set PE1 to push-pull */
    GPIOE->OSPEEDR &= (0x3 << (1 * 2));  /* set PE1 to slow speed */
    GPIOE->PUPDR &= ~(0x3 << (1 * 2)); /* set PE1 to no pull-up, no pull-down */


    while (1)
    {
        GPIOE->BSRR = (0x1 << 1); /* set PE1 high */
        delay();
        GPIOE->BSRR = (0x1 << (1 + 16)); /* set PE1 low */
        delay();

    }
}
