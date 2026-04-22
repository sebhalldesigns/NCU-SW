#ifndef NCUKIT_LIB_ANALOG_H
#define NCUKIT_LIB_ANALOG_H

#include <stdint.h>

#ifdef MATLAB_MEX_FILE
    /* Simulink run and check */
#else
    /* Embedded target build */
    #define STM32H745xx
    #include <stm32h7xx.h>
#endif

typedef enum {
    ANALOG_PIN_AIN0 = 0U, /* PA3  -> ADC1_INP15 */
    ANALOG_PIN_AIN1 = 1U, /* PA6  -> ADC1_INP3  */
    ANALOG_PIN_AIN2 = 2U, /* PB1  -> ADC1_INP5  */
    ANALOG_PIN_AIN3 = 3U  /* PC0  -> ADC1_INP10 */
} analog_pin_t;

/* Returns external input voltage in mV (after divider compensation). */
uint16_t analog_read(uint8_t pin);

#endif /* NCUKIT_LIB_ANALOG_H */
