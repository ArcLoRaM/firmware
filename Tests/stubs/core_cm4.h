/* Tests/stubs/core_cm4.h
 *
 * CMSIS core intrinsics stub. On ARM these expand to compiler built-ins or
 * inline assembly. On an x86 host GCC they become no-ops, which is exactly
 * right for logic-only tests — the test never runs on hardware. */

#ifndef __CORE_CM4_H_GENERIC
#define __CORE_CM4_H_GENERIC

#include <stdint.h>

static inline void     __disable_irq(void)           {}
static inline void     __enable_irq(void)            {}
static inline uint32_t __get_PRIMASK(void)           { return 0U; }
static inline void     __set_PRIMASK(uint32_t priMask) { (void)priMask; }
static inline void     __DSB(void)                   {}
static inline void     __ISB(void)                   {}
static inline void     __NOP(void)                   {}
static inline void     __WFI(void)                   {}
static inline void     __WFE(void)                   {}
static inline void     __SEV(void)                   {}

#endif /* __CORE_CM4_H_GENERIC */
