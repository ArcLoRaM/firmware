/* Tests/stubs/stm32wlxx_hal_rtc.h
 *
 * Minimal RTC stub — only the types and functions your Logic code calls.
 * If a compile error tells you a field or function is missing, add it here
 * following the same pattern (look up the real type in the HAL source and
 * copy just the field you need). */

#ifndef STM32WLxx_HAL_RTC_H
#define STM32WLxx_HAL_RTC_H

#include "stm32wlxx_hal_def.h"

typedef struct {
    uint8_t  Hours;
    uint8_t  Minutes;
    uint8_t  Seconds;
    uint32_t SubSeconds;
    uint8_t  DayLightSaving;
    uint8_t  StoreOperation;
} RTC_TimeTypeDef;

typedef struct {
    uint8_t WeekDay;
    uint8_t Month;
    uint8_t Date;
    uint8_t Year;
} RTC_DateTypeDef;

/* Opaque handle — Logic code should only pass a pointer to this */
typedef struct {
    uint32_t Instance;
} RTC_HandleTypeDef;

#define RTC_FORMAT_BIN 0x00U
#define RTC_FORMAT_BCD 0x01U

static inline HAL_StatusTypeDef HAL_RTC_GetTime(RTC_HandleTypeDef *h, RTC_TimeTypeDef *t, uint32_t fmt)
    { (void)h; (void)t; (void)fmt; return HAL_OK; }

static inline HAL_StatusTypeDef HAL_RTC_SetTime(RTC_HandleTypeDef *h, RTC_TimeTypeDef *t, uint32_t fmt)
    { (void)h; (void)t; (void)fmt; return HAL_OK; }

static inline HAL_StatusTypeDef HAL_RTC_GetDate(RTC_HandleTypeDef *h, RTC_DateTypeDef *d, uint32_t fmt)
    { (void)h; (void)d; (void)fmt; return HAL_OK; }

static inline HAL_StatusTypeDef HAL_RTC_SetDate(RTC_HandleTypeDef *h, RTC_DateTypeDef *d, uint32_t fmt)
    { (void)h; (void)d; (void)fmt; return HAL_OK; }

#endif /* STM32WLxx_HAL_RTC_H */
