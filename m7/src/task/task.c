/***************************************************************
**
** NCU Source File
**
** File         :  task.c
** Module       :  task
** Author       :  SH
** Created      :  2026-04-14 (YYYY-MM-DD)
** License      :  MIT
** Description  :  NCU Task Interface
**
***************************************************************/

/***************************************************************
** MARK: INCLUDES
***************************************************************/

#include "task.h"

/***************************************************************
** MARK: CONSTANTS & MACROS
***************************************************************/

#define TASK_A_PRIORITY (5)
#define TASK_B_PRIORITY (4)

/***************************************************************
** MARK: TYPEDEFS
***************************************************************/

/***************************************************************
** MARK: STATIC VARIABLES
***************************************************************/

static task_fn_t task_fns[3] = {
    NULL,   /* M7_TASK_A */
    NULL,   /* M7_TASK_B */
    NULL    /* M7_TASK_C */
};

/***************************************************************
** MARK: STATIC FUNCTION DEFS
***************************************************************/

/***************************************************************
** MARK: GLOBAL FUNCTIONS
***************************************************************/

bool task_init(uint32_t a_us, uint32_t b_us)
{
    /* Timer calcs:
    **  SYSCLK = 400MHz
    **  APB1/2 timers at 200MHz
    **
    **  If we set PSC to 200-1=199, then timer clock is 1MHz (1us tick)
    **  ARR should be set to desired interval in us - 1
    */

    RCC->APB2ENR |= RCC_APB2ENR_TIM16EN | RCC_APB2ENR_TIM17EN;
    (void)RCC->APB2ENR;
    
    TIM16->CR1 = 0;
    TIM16->PSC = 199;
    TIM16->ARR = a_us - 1;
    /* Latch PSC/ARR before starting to avoid a fast first period. */
    TIM16->EGR = TIM_EGR_UG;
    TIM16->SR = 0;
    TIM16->DIER |= TIM_DIER_UIE;
    NVIC_SetPriority(TIM16_IRQn, TASK_A_PRIORITY);
    NVIC_EnableIRQ(TIM16_IRQn);

    TIM17->CR1 = 0;
    TIM17->PSC = 199;
    TIM17->ARR = b_us - 1;
    /* Latch PSC/ARR before starting to avoid a fast first period. */
    TIM17->EGR = TIM_EGR_UG;
    TIM17->SR = 0;
    TIM17->DIER |= TIM_DIER_UIE;
    NVIC_SetPriority(TIM17_IRQn, TASK_B_PRIORITY);
    NVIC_EnableIRQ(TIM17_IRQn);

    /* Start timers */
    TIM16->CR1 |= TIM_CR1_CEN;
    TIM17->CR1 |= TIM_CR1_CEN;

    return true;
}

void task_register(task_id_t task_id, task_fn_t fn)
{
    if (task_id < 3)
    {
        task_fns[task_id] = fn;
    }
}

void task_run()
{
    while (1)
    {
        if (task_fns[M7_TASK_C])
        {
            task_fns[M7_TASK_C](sys_micros());
        }
    }
}

void TIM16_IRQHandler(void)
{
    if (TIM16->SR & TIM_SR_UIF)
    {
        TIM16->SR &= ~TIM_SR_UIF;

        if (task_fns[M7_TASK_A])
        {
            task_fns[M7_TASK_A](sys_micros());
        }
    } 
}

void TIM17_IRQHandler(void)
{
    if (TIM17->SR & TIM_SR_UIF)
    {
        TIM17->SR &= ~TIM_SR_UIF;

        if (task_fns[M7_TASK_B])
        {
            task_fns[M7_TASK_B](sys_micros());
        }
    } 
}


/***************************************************************
** MARK: STATIC FUNCTIONS
***************************************************************/

