/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    timer_if.h
  * @author  MCD Application Team
  * @brief  configuration of the timer_if.c instances
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __TIMER_IF_H__
#define __TIMER_IF_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32_timer.h"
#include "stm32_systime.h"

/* USER CODE BEGIN Includes */
#include <stdbool.h>
#include "protocol_types.h"
/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* External variables --------------------------------------------------------*/
/* USER CODE BEGIN EV */

/* USER CODE END EV */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
/**
  * @brief Init RTC hardware
  * @return Status based on @ref UTIL_TIMER_Status_t
  */
UTIL_TIMER_Status_t TIMER_IF_Init(void);

/**
  * @brief Set the alarm
  * @note The alarm is set at timeout from timer Reference (TimerContext)
  * @param timeout Duration of the Timer in ticks
  * @return Status based on @ref UTIL_TIMER_Status_t
  */
UTIL_TIMER_Status_t TIMER_IF_StartTimer(uint32_t timeout);

/**
  * @brief Stop the Alarm
  * @return Status based on @ref UTIL_TIMER_Status_t
  */
UTIL_TIMER_Status_t TIMER_IF_StopTimer(void);

/**
  * @brief set timer Reference (TimerContext)
  * @return  Timer Reference Value in  Ticks
  */
uint32_t TIMER_IF_SetTimerContext(void);

/**
  * @brief Get the RTC timer Reference
  * @return Timer Value in  Ticks
  */
uint32_t TIMER_IF_GetTimerContext(void);

/**
  * @brief Get the timer elapsed time since timer Reference (TimerContext) was set
  * @return RTC Elapsed time in ticks
  */
uint32_t TIMER_IF_GetTimerElapsedTime(void);

/**
  * @brief Get the timer value
  * @return RTC Timer value in ticks
  */
uint32_t TIMER_IF_GetTimerValue(void);

/**
  * @brief Return the minimum timeout in ticks the RTC is able to handle
  * @return minimum value for a timeout in ticks
  */
uint32_t TIMER_IF_GetMinimumTimeout(void);

/**
  * @brief a delay of delay ms by polling RTC
  * @param delay in ms
  */
void TIMER_IF_DelayMs(uint32_t delay);

/**
  * @brief converts time in ms to time in ticks
  * @param[in] timeMilliSec time in milliseconds
  * @return time in timer ticks
  */
uint32_t TIMER_IF_Convert_ms2Tick(uint32_t timeMilliSec);

/**
  * @brief converts time in ticks to time in ms
  * @param[in] tick time in timer ticks
  * @return time in timer milliseconds
  */
uint32_t TIMER_IF_Convert_Tick2ms(uint32_t tick);

/**
  * @brief Get rtc time
  * @param[out] subSeconds in ticks
  * @return time seconds
  */
uint32_t TIMER_IF_GetTime(uint16_t *subSeconds);

/**
  * @brief write seconds in backUp register
  * @note Used to store seconds difference between RTC time and Unix time
  * @param[in] Seconds time in seconds
  */
void TIMER_IF_BkUp_Write_Seconds(uint32_t Seconds);

/**
  * @brief reads seconds from backUp register
  * @note Used to store seconds difference between RTC time and Unix time
  * @return Time in seconds
  */
uint32_t TIMER_IF_BkUp_Read_Seconds(void);

/**
  * @brief writes SubSeconds in backUp register
  * @note Used to store SubSeconds difference between RTC time and Unix time
  * @param[in] SubSeconds time in SubSeconds
  */
void TIMER_IF_BkUp_Write_SubSeconds(uint32_t SubSeconds);

/**
  * @brief reads SubSeconds from backUp register
  * @note Used to store SubSeconds difference between RTC time and Unix time
  * @return Time in SubSeconds
  */
uint32_t TIMER_IF_BkUp_Read_SubSeconds(void);

/* USER CODE BEGIN EFP */
/**
 * @brief Read the live RTC calendar into a FrameEpoch_t using BCD format.
 * @note  Useful for relay TX (fill SyncPayload directly) and post-set diagnostics.
 *        Calendar fields are BCD; subseconds is raw SSR (binary, counts DOWN from PREDIV_S).
 * @param[out] out  Destination struct — must not be NULL.
 */
void RTC_GetCalendarBcd(FrameEpoch_t *out);

/**
 * @brief Monotonic milliseconds: the RTC time of day extended across midnight.
 * @note  Time base of UTIL_TIMER and HAL_GetTick. Never goes back (an RTC
 *        written back is not counted); wraps at 2^32 like any tick counter.
 *        TIMER_IF_GetTimerValue stays the time of day.
 */
uint32_t TIMER_IF_GetMonotonicMs(void);

/**
 * @brief Time of day in RTC ticks (1/(PREDIV_S+1) s), read from the registers.
 * @note  Same clock as TIMER_IF_GetTimerValue, at tick resolution and in a
 *        few microseconds. With at_edge, waits for the next tick first (at
 *        most one tick, 244 us): the value is then exact at the return.
 */
uint32_t TIMER_IF_GetDayTicks(bool at_edge);

/**
 * @brief Call right before writing the RTC (calendar set or SHIFTR): brings
 *        the monotonic counter up to date and holds it during the write.
 * @retval true  No shift is pending: a SHIFTR may be written.
 * @retval false A shift is still pending (SHPF): do not write SHIFTR.
 */
bool TIMER_IF_RtcWriteBegin(void);

/**
 * @brief Call right after the RTC write, even if it failed or was skipped.
 *        The write is excluded from the monotonic counter.
 * @param calendar_set true if HAL_RTC_SetTime / SetDate was called.
 * @param shift_ms     Signed change of the reading a SHIFTR written in this
 *                     write makes (ms, + = advance), 0 if none was written.
 */
void TIMER_IF_RtcWriteEnd(bool calendar_set, int32_t shift_ms);
/* USER CODE END EFP */

#ifdef __cplusplus
}
#endif

#endif /* __TIMER_IF_H__ */
