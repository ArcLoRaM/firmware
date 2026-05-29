/* Tests/stubs/stm32wlxx_hal.h
 *
 * Top-level HAL stub. The real stm32wlxx_hal.h includes stm32wlxx_hal_conf.h
 * which conditionally includes every module header. On the host we only
 * provide the fundamental types and two tick functions — module-specific
 * types are in their own stub headers (stm32wlxx_hal_rtc.h, etc.). */

#ifndef __STM32WLxx_HAL_H
#define __STM32WLxx_HAL_H

#include "stm32wlxx_hal_def.h"

/* HAL_GetTick() returns milliseconds since boot in the real HAL.
 * Returning 0 is correct for unit tests — Logic code must not rely on
 * real elapsed time. */
static inline uint32_t HAL_GetTick(void)          { return 0U; }
static inline void     HAL_Delay(uint32_t ms)     { (void)ms;  }

#endif /* __STM32WLxx_HAL_H */
