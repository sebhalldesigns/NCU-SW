/***************************************************************
**
** NCU Header File
**
** File         :  task.h
** Module       :  task
** Author       :  SH
** Created      :  2026-04-14 (YYYY-MM-DD)
** License      :  MIT
** Description  :  NCU Task Interface
**
***************************************************************/

#ifndef TASK_H
#define TASK_H

/***************************************************************
** MARK: INCLUDES
***************************************************************/

#include <stdint.h>
#include <sys/sys.h>

/***************************************************************
** MARK: CONSTANTS & MACROS
***************************************************************/

/***************************************************************
** MARK: TYPEDEFS
***************************************************************/

typedef void (*task_fn_t)(uint32_t time_us);

typedef enum
{
    M7_TASK_A = 0,
    M7_TASK_B = 1,
    M7_TASK_C = 2
} task_id_t;

/***************************************************************
** MARK: FUNCTION DEFS
***************************************************************/

bool task_init(uint32_t a_us, uint32_t b_us);
void task_register(task_id_t task_id, task_fn_t fn);
void task_run();

#endif /* TASK_H */
