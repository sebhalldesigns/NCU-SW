/*
 * Academic License - for use in teaching, academic research, and meeting
 * course requirements at degree granting institutions only.  Not for
 * government, commercial, or other organizational use.
 *
 * File: app.h
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

#ifndef RTW_HEADER_app_h_
#define RTW_HEADER_app_h_
#ifndef app_COMMON_INCLUDES_
#define app_COMMON_INCLUDES_
#include "rtwtypes.h"
#endif                                 /* app_COMMON_INCLUDES_ */

#include "app_types.h"
#include "rt_defines.h"
#include <stddef.h>

/* Macros for accessing real-time model data structure */
#ifndef rtmGetErrorStatus
#define rtmGetErrorStatus(rtm)         ((rtm)->errorStatus)
#endif

#ifndef rtmSetErrorStatus
#define rtmSetErrorStatus(rtm, val)    ((rtm)->errorStatus = (val))
#endif

/* Real-time Model Data Structure */
struct NCU_APP_tag_RTM_app_t {
  const char_T * volatile errorStatus;
};

/* Model entry point functions */
extern void app_initialize(NCU_APP_RT_MODEL_app_t *const NCU_APP_app_M, real32_T
  *NCU_APP_app_U_input_signal, real32_T *NCU_APP_app_Y_output_signal);
extern void app_step(NCU_APP_RT_MODEL_app_t *const NCU_APP_app_M, real32_T
                     NCU_APP_app_U_input_signal, real32_T
                     *NCU_APP_app_Y_output_signal);
extern void app_terminate(NCU_APP_RT_MODEL_app_t *const NCU_APP_app_M);

/*-
 * The generated code includes comments that allow you to trace directly
 * back to the appropriate location in the model.  The basic format
 * is <system>/block_name, where system is the system number (uniquely
 * assigned by Simulink) and block_name is the name of the block.
 *
 * Use the MATLAB hilite_system command to trace the generated code back
 * to the model.  For example,
 *
 * hilite_system('<S3>')    - opens system 3
 * hilite_system('<S3>/Kp') - opens and selects block Kp which resides in S3
 *
 * Here is the system hierarchy for this model
 *
 * '<Root>' : 'app'
 */
#endif                                 /* RTW_HEADER_app_h_ */

/*
 * File trailer for generated code.
 *
 * [EOF]
 */
