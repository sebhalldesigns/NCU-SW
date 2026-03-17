/*
 * Academic License - for use in teaching, academic research, and meeting
 * course requirements at degree granting institutions only.  Not for
 * government, commercial, or other organizational use.
 *
 * File: app.c
 *
 * Code generated for Simulink model 'app'.
 *
 * Model version                  : 1.6
 * Simulink Coder version         : 23.2 (R2023b) 01-Aug-2023
 * C/C++ source code generated on : Tue Mar 17 23:15:49 2026
 *
 * Target selection: ert.tlc
 * Embedded hardware selection: ARM Compatible->ARM Cortex-M
 * Code generation objectives: Unspecified
 * Validation result: Not run
 */

#include "app.h"
#include "rtwtypes.h"

/* Model step function */
void app_step(NCU_APP_RT_MODEL_app_t *const NCU_APP_app_M, real32_T
              NCU_APP_app_U_input_signal, real32_T *NCU_APP_app_Y_output_signal)
{
  /* Outport: '<Root>/output_signal' incorporates:
   *  Gain: '<Root>/Gain'
   *  Inport: '<Root>/input_signal'
   */
  *NCU_APP_app_Y_output_signal = 2.0F * NCU_APP_app_U_input_signal;
  UNUSED_PARAMETER(NCU_APP_app_M);
}

/* Model initialize function */
void app_initialize(NCU_APP_RT_MODEL_app_t *const NCU_APP_app_M, real32_T
                    *NCU_APP_app_U_input_signal, real32_T
                    *NCU_APP_app_Y_output_signal)
{
  /* Registration code */

  /* initialize error status */
  rtmSetErrorStatus(NCU_APP_app_M, (NULL));

  /* external inputs */
  *NCU_APP_app_U_input_signal = 0.0F;

  /* external outputs */
  *NCU_APP_app_Y_output_signal = 0.0F;
  UNUSED_PARAMETER(NCU_APP_app_M);
}

/* Model terminate function */
void app_terminate(NCU_APP_RT_MODEL_app_t *const NCU_APP_app_M)
{
  /* (no terminate code required) */
  UNUSED_PARAMETER(NCU_APP_app_M);
}

/*
 * File trailer for generated code.
 *
 * [EOF]
 */
