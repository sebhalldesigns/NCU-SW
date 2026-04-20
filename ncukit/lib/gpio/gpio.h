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

#define GPIO_PIN_GPIO0 0U /* PA4 */
#define GPIO_PIN_GPIO1 1U /* PA5 */
#define GPIO_PIN_GPIO2 2U /* PA0 */
#define GPIO_PIN_GPIO3 3U /* PA1 */
#define GPIO_PIN_STATUS_LED 4U /* PE15 */

void gpio_write(uint8_t pin, bool value);
bool gpio_read(uint8_t pin);

#endif /* GPIO_H */
