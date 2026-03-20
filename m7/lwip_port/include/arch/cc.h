#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

#include <stdint.h>
#include <stdio.h>

/* Data types lwIP expects */
typedef uint8_t     u8_t;
typedef int8_t      s8_t;
typedef uint16_t    u16_t;
typedef int16_t     s16_t;
typedef uint32_t    u32_t;
typedef int32_t     s32_t;
typedef uintptr_t   mem_ptr_t;

/* Printf formatters */
#define U16_F "hu"
#define S16_F "hd"
#define X16_F "hx"
#define U32_F "lu"
#define S32_F "ld"
#define X32_F "lx"

/* Endianness - STM32 is little endian */
#define BYTE_ORDER LITTLE_ENDIAN

/* No file support needed */
#define LWIP_PLATFORM_ASSERT(x) do { (void)(x); } while(0)

/* Critical sections - we'll use simple interrupt disable for now */
#define LWIP_PLATFORM_DIAG(x) do { printf x; } while(0)

extern void sys_arch_protect(void);
extern void sys_arch_unprotect(void);
#define SYS_ARCH_PROTECT(lev)    sys_arch_protect()
#define SYS_ARCH_UNPROTECT(lev)  sys_arch_unprotect()
#define SYS_ARCH_DECL_PROTECT(lev)

#endif /* LWIP_ARCH_CC_H */