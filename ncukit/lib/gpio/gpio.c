#include "gpio.h"   

#ifdef MATLAB_MEX_FILE
/* simulink run and check */

void gpio_init(uint8_t pin, bool input) { }
void gpio_write(uint8_t pin, bool value) { }
bool gpio_read(uint8_t pin) { return false; }

#else

/* embedded target build */
#define STM32H745xx
#include <stm32h7xx.h>

typedef struct {
    GPIO_TypeDef *port;
    uint8_t bit;
} gpio_desc_t;

static bool gpio_get_desc(uint8_t pin, gpio_desc_t *d);
static void gpio_enable_clock(GPIO_TypeDef *port);

void gpio_init(uint8_t pin, bool input) 
{
     gpio_desc_t d;
    if (!gpio_get_desc(pin, &d)) return;

    gpio_enable_clock(d.port);

    /* MODER: 00=input, 01=output */
    d.port->MODER &= ~(0x3UL << (2UL * d.bit));
    if (!input) {
        d.port->MODER |=  (0x1UL << (2UL * d.bit)); /* output */
        d.port->OTYPER &= ~(1UL << d.bit);          /* push-pull */
        d.port->OSPEEDR &= ~(0x3UL << (2UL * d.bit)); /* low speed */
    }

    /* No pull by default */
    d.port->PUPDR &= ~(0x3UL << (2UL * d.bit));
}

void gpio_write(uint8_t pin, bool value) 
{ 
    gpio_desc_t d;
    if (!gpio_get_desc(pin, &d)) return;

    if (value) {
        d.port->BSRR = (1UL << d.bit);          /* set */
    } else {
        d.port->BSRR = (1UL << (d.bit + 16UL)); /* reset */
    }
}

bool gpio_read(uint8_t pin) 
{ 
    gpio_desc_t d;
    if (!gpio_get_desc(pin, &d)) return false;

    return ((d.port->IDR & (1UL << d.bit)) != 0U);
}

static bool gpio_get_desc(uint8_t pin, gpio_desc_t *d)
{
    switch (pin) {
        case GPIO_PIN_LED1:   d->port = GPIOB; d->bit = 0U;  return true;  /* PB0  */
        case GPIO_PIN_LED2:   d->port = GPIOE; d->bit = 1U;  return true;  /* PE1  */
        case GPIO_PIN_LED3:   d->port = GPIOB; d->bit = 14U; return true;  /* PB14 */
        case GPIO_PIN_BUTTON: d->port = GPIOC; d->bit = 13U; return true;  /* PC13 */
        default: return false;
    }
}

static void gpio_enable_clock(GPIO_TypeDef *port)
{
    if (port == GPIOB) RCC->AHB4ENR |= RCC_AHB4ENR_GPIOBEN;
    if (port == GPIOC) RCC->AHB4ENR |= RCC_AHB4ENR_GPIOCEN;
    if (port == GPIOE) RCC->AHB4ENR |= RCC_AHB4ENR_GPIOEEN;
    (void)RCC->AHB4ENR; /* ensure write completes before GPIO access */
}


#endif


