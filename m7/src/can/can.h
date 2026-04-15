/***************************************************************
**
** NCU Header File
**
** File         :  can.h
** Module       :  can
** Author       :  Codex
** Created      :  2026-04-15 (YYYY-MM-DD)
** License      :  MIT
** Description  :  M7 FDCAN test interface
**
***************************************************************/

#ifndef CAN_H
#define CAN_H

/***************************************************************
** MARK: INCLUDES
***************************************************************/

#include <stdbool.h>
#include <stdint.h>

/***************************************************************
** MARK: FUNCTION DEFS
***************************************************************/

bool can_init(void);
void can_poll(uint32_t time_us);

#endif /* CAN_H */
