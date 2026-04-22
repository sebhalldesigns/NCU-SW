#include "analog.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef MATLAB_MEX_FILE
/* Simulink run and check */

uint16_t analog_read(uint8_t pin)
{
    (void)pin;
    return 0U;
}

#else

/* Embedded target build */
#define STM32H745xx
#include <stm32h7xx.h>

#define ADC_TIMEOUT_LOOPS        1000000U
#define ADC_RAW_FULL_SCALE       4095U
#define ADC_INPUT_FULL_SCALE_MV  5000U

typedef struct {
    GPIO_TypeDef *port;
    uint8_t bit;
    uint8_t channel;
} analog_desc_t;

static bool analog_ready = false;

static bool analog_get_desc(uint8_t pin, analog_desc_t *desc);
static void analog_enable_gpio_clock(GPIO_TypeDef *port);
static void analog_prepare_pin(const analog_desc_t *desc);
static bool analog_wait_for_clear(volatile uint32_t *reg, uint32_t mask);
static bool analog_wait_for_set(volatile uint32_t *reg, uint32_t mask);
static bool analog_init(void);

uint16_t analog_read(uint8_t pin)
{
    analog_desc_t desc;
    uint32_t raw;
    uint32_t mv;

    if (!analog_get_desc(pin, &desc)) {
        return 0U;
    }

    if (!analog_init()) {
        return 0U;
    }

    ADC1->SQR1 = ((uint32_t)desc.channel << ADC_SQR1_SQ1_Pos); /* single conversion (L=0) */
    ADC1->ISR = ADC_ISR_EOC | ADC_ISR_EOS;
    ADC1->CR |= ADC_CR_ADSTART;

    if (!analog_wait_for_set(&ADC1->ISR, ADC_ISR_EOC)) {
        return 0U;
    }

    raw = (uint32_t)(ADC1->DR & 0xFFFFU);
    if (raw > ADC_RAW_FULL_SCALE) {
        raw = ADC_RAW_FULL_SCALE;
    }

    /* Divider compensation: MCU sees 3.0 V at external 5.0 V input. */
    mv = ((raw * ADC_INPUT_FULL_SCALE_MV) + (ADC_RAW_FULL_SCALE / 2U)) / ADC_RAW_FULL_SCALE;
    if (mv > 0xFFFFU) {
        mv = 0xFFFFU;
    }

    return (uint16_t)mv;
}

static bool analog_init(void)
{
    analog_desc_t desc;
    uint32_t pcsel_mask;

    if (analog_ready) {
        return true;
    }

    RCC->AHB1ENR |= RCC_AHB1ENR_ADC12EN;
    (void)RCC->AHB1ENR;

    /* Configure all AIN pins to analog mode once. */
    if (analog_get_desc(ANALOG_PIN_AIN0, &desc)) {
        analog_prepare_pin(&desc);
    }
    if (analog_get_desc(ANALOG_PIN_AIN1, &desc)) {
        analog_prepare_pin(&desc);
    }
    if (analog_get_desc(ANALOG_PIN_AIN2, &desc)) {
        analog_prepare_pin(&desc);
    }
    if (analog_get_desc(ANALOG_PIN_AIN3, &desc)) {
        analog_prepare_pin(&desc);
    }

    ADC1->CR &= ~ADC_CR_DEEPPWD;
    ADC1->CR |= ADC_CR_ADVREGEN;
    for (volatile uint32_t i = 0U; i < 1000U; i++) {
        __NOP();
    }

    if ((ADC1->CR & ADC_CR_ADEN) != 0U) {
        ADC1->CR |= ADC_CR_ADDIS;
        if (!analog_wait_for_clear(&ADC1->CR, ADC_CR_ADEN)) {
            return false;
        }
    }

    ADC12_COMMON->CCR = (ADC12_COMMON->CCR & ~(ADC_CCR_CKMODE | ADC_CCR_PRESC)) | ADC_CCR_CKMODE;

    ADC1->DIFSEL = 0U;
    ADC1->CFGR = 0U;
    ADC1->CFGR2 = 0U;
    ADC1->CR = (ADC1->CR & ~ADC_CR_BOOST) | ADC_CR_BOOST_1;

    /* Long sample time on all used channels for stable readings. */
    ADC1->SMPR1 = (ADC1->SMPR1 & ~(ADC_SMPR1_SMP3 | ADC_SMPR1_SMP5))
                | (ADC_SMPR1_SMP3_0 | ADC_SMPR1_SMP3_1 | ADC_SMPR1_SMP3_2)
                | (ADC_SMPR1_SMP5_0 | ADC_SMPR1_SMP5_1 | ADC_SMPR1_SMP5_2);
    ADC1->SMPR2 = (ADC1->SMPR2 & ~(ADC_SMPR2_SMP10 | ADC_SMPR2_SMP15))
                | (ADC_SMPR2_SMP10_0 | ADC_SMPR2_SMP10_1 | ADC_SMPR2_SMP10_2)
                | (ADC_SMPR2_SMP15_0 | ADC_SMPR2_SMP15_1 | ADC_SMPR2_SMP15_2);

    pcsel_mask = (1UL << 15U) | (1UL << 3U) | (1UL << 5U) | (1UL << 10U);
    ADC1->PCSEL = pcsel_mask;

    ADC1->CR |= ADC_CR_ADCAL;
    if (!analog_wait_for_clear(&ADC1->CR, ADC_CR_ADCAL)) {
        return false;
    }

    ADC1->ISR = ADC_ISR_ADRDY;
    ADC1->CR |= ADC_CR_ADEN;
    if (!analog_wait_for_set(&ADC1->ISR, ADC_ISR_ADRDY)) {
        return false;
    }

    analog_ready = true;
    return true;
}

static bool analog_get_desc(uint8_t pin, analog_desc_t *desc)
{
    if (desc == NULL) {
        return false;
    }

    switch (pin) {
        case ANALOG_PIN_AIN0: desc->port = GPIOA; desc->bit = 3U; desc->channel = 15U; return true;
        case ANALOG_PIN_AIN1: desc->port = GPIOA; desc->bit = 6U; desc->channel = 3U;  return true;
        case ANALOG_PIN_AIN2: desc->port = GPIOB; desc->bit = 1U; desc->channel = 5U;  return true;
        case ANALOG_PIN_AIN3: desc->port = GPIOC; desc->bit = 0U; desc->channel = 10U; return true;
        default: return false;
    }
}

static void analog_enable_gpio_clock(GPIO_TypeDef *port)
{
    if (port == GPIOA) RCC->AHB4ENR |= RCC_AHB4ENR_GPIOAEN;
    if (port == GPIOB) RCC->AHB4ENR |= RCC_AHB4ENR_GPIOBEN;
    if (port == GPIOC) RCC->AHB4ENR |= RCC_AHB4ENR_GPIOCEN;
    (void)RCC->AHB4ENR;
}

static void analog_prepare_pin(const analog_desc_t *desc)
{
    uint32_t shift;

    if (desc == NULL) {
        return;
    }

    analog_enable_gpio_clock(desc->port);

    shift = 2UL * (uint32_t)desc->bit;
    desc->port->MODER = (desc->port->MODER & ~(0x3UL << shift)) | (0x3UL << shift); /* analog */
    desc->port->PUPDR &= ~(0x3UL << shift); /* no pull */
}

static bool analog_wait_for_clear(volatile uint32_t *reg, uint32_t mask)
{
    uint32_t timeout = ADC_TIMEOUT_LOOPS;

    while (((*reg) & mask) != 0U) {
        if (timeout-- == 0U) {
            return false;
        }
    }
    return true;
}

static bool analog_wait_for_set(volatile uint32_t *reg, uint32_t mask)
{
    uint32_t timeout = ADC_TIMEOUT_LOOPS;

    while (((*reg) & mask) != mask) {
        if (timeout-- == 0U) {
            return false;
        }
    }
    return true;
}

#endif
