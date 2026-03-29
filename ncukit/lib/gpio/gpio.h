#ifndef GPIO_H
#define GPIO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef MATLAB_MEX_FILE
    /* simulink run and check */
#else
    /* embedded target build */
    #define STM32H745xx
    #include <stm32h7xx.h>
#endif

#define GPIO_PIN_LED1 0U
#define GPIO_PIN_LED2 1U
#define GPIO_PIN_LED3 2U
#define GPIO_PIN_BUTTON 3U

void gpio_init(uint8_t pin, bool input);
void gpio_write(uint8_t pin, bool value);
bool gpio_read(uint8_t pin);

#endif /* GPIO_H */