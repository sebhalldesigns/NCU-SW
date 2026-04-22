#ifndef NCU_ICC_H
#define NCU_ICC_H

#include <stdint.h>

/* HSEM used by M4 to indicate control task activity:
 * locked   -> M4 app_step() is running (do not read M4 app memory)
 * unlocked -> M4 is between steps (safe read window)
 */
#define ICC_M4_ACTIVITY_HSEM_ID        (0U)
#define ICC_M4_ACTIVITY_PROCESS_ID     (1U)

/* Deferred XCP upload wait budget on M7 */
#define ICC_XCP_UPLOAD_TIMEOUT_US      (50000U)

/* STM32H745/755 shared SRAM ranges used for M4 application data. */
#define ICC_M4_SRAM_ALIAS_BASE         (0x10000000UL)
#define ICC_M4_SRAM_BUS_BASE           (0x30000000UL)
#define ICC_M4_SRAM_SIZE_BYTES         (288UL * 1024UL)

#define ICC_D3_SRAM_BASE               (0x38000000UL)
#define ICC_D3_SRAM_SIZE_BYTES         (64UL * 1024UL)

/* M4 app flash bank (CM4 image at 0x0810_0000). */
#define ICC_M4_FLASH_BASE              (0x08100000UL)
#define ICC_M4_FLASH_SIZE_BYTES        (1024UL * 1024UL)

#endif /* NCU_ICC_H */
