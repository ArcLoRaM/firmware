/* Tests/stubs/stm32wlxx_hal_def.h
 *
 * Minimal stub for the HAL type definitions header.
 * The real stm32wlxx_hal_def.h pulls in CMSIS core_cm4.h which contains
 * ARM-specific assembly intrinsics that cannot compile on a host x86 GCC.
 * This stub replaces the entire chain with plain-C equivalents.
 *
 * Add types here only as your Logic code needs them. */

#ifndef STM32WLxx_HAL_DEF_H
#define STM32WLxx_HAL_DEF_H

#include <stdint.h>
#include <stddef.h>

/* HAL return codes — values must match the real HAL */
typedef enum {
    HAL_OK      = 0x00U,
    HAL_ERROR   = 0x01U,
    HAL_BUSY    = 0x02U,
    HAL_TIMEOUT = 0x03U
} HAL_StatusTypeDef;

typedef enum {
    HAL_UNLOCKED = 0x00U,
    HAL_LOCKED   = 0x01U
} HAL_LockTypeDef;

/* Volatile qualifiers used throughout the HAL */
#define __IO   volatile
#define __I    volatile const
#define __O    volatile

/* __weak = allow a function to be overridden by a stronger-linked definition */
#define __weak __attribute__((weak))

#endif /* STM32WLxx_HAL_DEF_H */
