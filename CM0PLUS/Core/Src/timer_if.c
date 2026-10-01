/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    timer_if.c
  * @author  MCD Application Team
  * @brief   Configure RTC Alarm, Tick and Calendar manager
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

/* Includes ------------------------------------------------------------------*/
#include <math.h>
#include "timer_if.h"

/* USER CODE BEGIN Includes */
#include <time.h>   /* mktime */
#include "rtc.h"    /* hrtc handle */
#include "main.h"   /* RTC_PREDIV_S, Error_Handler */
#include "day_ms.h" /* DayMsClock_t: monotonic ms across midnight */
/* USER CODE END Includes */

/* External variables ---------------------------------------------------------*/

/**
  * @brief Timer driver callbacks handler
  */
const UTIL_TIMER_Driver_s UTIL_TimerDriver =
{
  TIMER_IF_Init,
  NULL,

  TIMER_IF_StartTimer,
  TIMER_IF_StopTimer,

  TIMER_IF_SetTimerContext,
  TIMER_IF_GetTimerContext,

  TIMER_IF_GetTimerElapsedTime,
  TIMER_IF_GetTimerValue,
  TIMER_IF_GetMinimumTimeout,

  TIMER_IF_Convert_ms2Tick,
  TIMER_IF_Convert_Tick2ms,
};

/**
  * @brief SysTime driver callbacks handler
  */
const UTIL_SYSTIM_Driver_s UTIL_SYSTIMDriver =
{
  TIMER_IF_BkUp_Write_Seconds,
  TIMER_IF_BkUp_Read_Seconds,
  TIMER_IF_BkUp_Write_SubSeconds,
  TIMER_IF_BkUp_Read_SubSeconds,
  TIMER_IF_GetTime,
};

/* USER CODE BEGIN EV */

/* USER CODE END EV */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define MIN_ALARM_DELAY  3u        /* minimum WUT ticks before firing (~1.5 ms at 2048 Hz) */
#define WUT_CLOCK_HZ     2048u     /* WUT rate: RTCCLK/16 = 32768/16 Hz */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
#ifndef UTIL_TIMER_IRQ_MAP_INIT
#define UTIL_TIMER_IRQ_MAP_INIT()
#endif
#ifndef UTIL_TIMER_IRQ_MAP_PROCESS
#define UTIL_TIMER_IRQ_MAP_PROCESS()  UTIL_TIMER_IRQ_Handler()
#endif
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/**
  * @brief RtcTimerContext
  */
static uint32_t RtcTimerContext = 0;

/* USER CODE BEGIN PV */
static uint8_t RTC_Initialized = 0;
/* Monotonic time base of UTIL_TIMER and HAL_GetTick. GetTimerTicks() is the
 * time of day and falls back at midnight and on RTC corrections; the timer
 * context, elapsed time and HAL tick must not (issue #54). */
static DayMsClock_t s_mono_clock;
/* Held during an RTC write (TIMER_IF_RtcWriteBegin / End): the calendar is
 * mid-update, so the counter is read without touching the RTC. */
static volatile uint8_t s_mono_held = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
static inline uint32_t GetTimerTicks(void);
static uint32_t GetMonotonicTicks(void);
static void ReadClock(uint32_t *day_ms, bool *shift_pending);
static inline int32_t SubSecondsToMs(uint32_t ssr);
/* USER CODE END PFP */

/* Exported functions ---------------------------------------------------------*/
UTIL_TIMER_Status_t TIMER_IF_Init(void)
{
  UTIL_TIMER_Status_t ret = UTIL_TIMER_OK;
  /* USER CODE BEGIN TIMER_IF_Init */
  if (RTC_Initialized == 0)
  {
    hrtc.IsEnabled.RtcFeatures = UINT32_MAX;
    MX_RTC_Init();
    TIMER_IF_StopTimer();
    HAL_RTCEx_EnableBypassShadow(&hrtc);
    hrtc.IsEnabled.RtcFeatures = UINT32_MAX;
    DayMsClock_Init(&s_mono_clock, GetTimerTicks());
    RtcTimerContext = s_mono_clock.mono_ms;
    RTC_Initialized = 1;
  }
  /* USER CODE END TIMER_IF_Init */
  return ret;
}

UTIL_TIMER_Status_t TIMER_IF_StartTimer(uint32_t timeout)
{
  UTIL_TIMER_Status_t ret = UTIL_TIMER_OK;
  /* USER CODE BEGIN TIMER_IF_StartTimer */
  TIMER_IF_StopTimer();
  timeout += RtcTimerContext;

  /* Context and now are both monotonic, so a context set before midnight
   * stays valid after it. A deadline already passed fires at once. */
  int32_t  left         = (int32_t)(timeout - GetMonotonicTicks());
  uint32_t remaining_ms = (left > 0) ? (uint32_t)left : 1u;

  /* Convert ms to WUT 2048 Hz counts; clamp to 16-bit hardware max (~32 s) */
  uint32_t count = (uint32_t)(((uint64_t)remaining_ms * WUT_CLOCK_HZ) / 1000u);
  if (count == 0u) { count = 1u; }
  if (count > 0xFFFFu) { count = 0xFFFFu; }

  if (HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, count, RTC_WAKEUPCLOCK_RTCCLK_DIV16, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE END TIMER_IF_StartTimer */
  return ret;
}

UTIL_TIMER_Status_t TIMER_IF_StopTimer(void)
{
  UTIL_TIMER_Status_t ret = UTIL_TIMER_OK;
  /* USER CODE BEGIN TIMER_IF_StopTimer */
  hrtc.IsEnabled.RtcFeatures = UINT32_MAX;
  HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
  hrtc.IsEnabled.RtcFeatures = UINT32_MAX;
  /* USER CODE END TIMER_IF_StopTimer */
  return ret;
}

uint32_t TIMER_IF_SetTimerContext(void)
{
  /* USER CODE BEGIN TIMER_IF_SetTimerContext */
  RtcTimerContext = GetMonotonicTicks();
  /* USER CODE END TIMER_IF_SetTimerContext */

  /*return time context*/
  return RtcTimerContext;
}

uint32_t TIMER_IF_GetTimerContext(void)
{
  /* USER CODE BEGIN TIMER_IF_GetTimerContext */

  /* USER CODE END TIMER_IF_GetTimerContext */

  /*return time context*/
  return RtcTimerContext;
}

uint32_t TIMER_IF_GetTimerElapsedTime(void)
{
  uint32_t ret = 0;
  /* USER CODE BEGIN TIMER_IF_GetTimerElapsedTime */
  ret = (uint32_t)(GetMonotonicTicks() - RtcTimerContext);
  /* USER CODE END TIMER_IF_GetTimerElapsedTime */
  return ret;
}

uint32_t TIMER_IF_GetTimerValue(void)
{
  uint32_t ret = 0;
  /* USER CODE BEGIN TIMER_IF_GetTimerValue */
  /* The time of day (UTIL_TIMER_GetCurrentTime): the RTC domain the TDMA
   * Machine and the MAC work in. Timer arithmetic uses the monotonic base. */
  ret = GetTimerTicks();
  /* USER CODE END TIMER_IF_GetTimerValue */
  return ret;
}

uint32_t TIMER_IF_GetMinimumTimeout(void)
{
  uint32_t ret = 0;
  /* USER CODE BEGIN TIMER_IF_GetMinimumTimeout */
  ret = MIN_ALARM_DELAY;
  /* USER CODE END TIMER_IF_GetMinimumTimeout */
  return ret;
}

uint32_t TIMER_IF_Convert_ms2Tick(uint32_t timeMilliSec)
{
  uint32_t ret = 0;
  /* USER CODE BEGIN TIMER_IF_Convert_ms2Tick */
  ret = timeMilliSec;   /* tick unit = 1 ms (SysTick rate); identity conversion */
  /* USER CODE END TIMER_IF_Convert_ms2Tick */
  return ret;
}

uint32_t TIMER_IF_Convert_Tick2ms(uint32_t tick)
{
  uint32_t ret = 0;
  /* USER CODE BEGIN TIMER_IF_Convert_Tick2ms */
  ret = tick;           /* tick unit = 1 ms; identity conversion */
  /* USER CODE END TIMER_IF_Convert_Tick2ms */
  return ret;
}

void TIMER_IF_DelayMs(uint32_t delay)
{
  /* USER CODE BEGIN TIMER_IF_DelayMs */
  uint32_t delayTicks = TIMER_IF_Convert_ms2Tick(delay);
  uint32_t timeout    = GetMonotonicTicks();
  while ((GetMonotonicTicks() - timeout) < delayTicks)
  {
    __NOP();
  }
  /* USER CODE END TIMER_IF_DelayMs */
}

uint32_t TIMER_IF_GetTime(uint16_t *mSeconds)
{
  uint32_t seconds = 0;
  /* USER CODE BEGIN TIMER_IF_GetTime */
  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};
  hrtc.IsEnabled.RtcFeatures = UINT32_MAX;
  HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
  HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

  int32_t subsec_ms = SubSecondsToMs(sTime.SubSeconds);

  struct tm t = {0};
  t.tm_year  = sDate.Year + 100;
  t.tm_mon   = sDate.Month - 1;
  t.tm_mday  = sDate.Date;
  t.tm_hour  = sTime.Hours;
  t.tm_min   = sTime.Minutes;
  t.tm_sec   = sTime.Seconds;
  t.tm_isdst = -1;

  seconds = (uint32_t)mktime(&t);
  if (subsec_ms < 0)
  {
    /* Right after an ADD1S shift: the calendar is one second ahead. */
    seconds--;
    subsec_ms += 1000;
  }
  if (mSeconds != NULL)
  {
    *mSeconds = (uint16_t)subsec_ms;
  }
  /* USER CODE END TIMER_IF_GetTime */
  return seconds;
}

void TIMER_IF_BkUp_Write_Seconds(uint32_t Seconds)
{
  /* USER CODE BEGIN TIMER_IF_BkUp_Write_Seconds */

  /* USER CODE END TIMER_IF_BkUp_Write_Seconds */
}

void TIMER_IF_BkUp_Write_SubSeconds(uint32_t SubSeconds)
{
  /* USER CODE BEGIN TIMER_IF_BkUp_Write_SubSeconds */

  /* USER CODE END TIMER_IF_BkUp_Write_SubSeconds */
}

uint32_t TIMER_IF_BkUp_Read_Seconds(void)
{
  uint32_t ret = 0;
  /* USER CODE BEGIN TIMER_IF_BkUp_Read_Seconds */

  /* USER CODE END TIMER_IF_BkUp_Read_Seconds */
  return ret;
}

uint32_t TIMER_IF_BkUp_Read_SubSeconds(void)
{
  uint32_t ret = 0;
  /* USER CODE BEGIN TIMER_IF_BkUp_Read_SubSeconds */

  /* USER CODE END TIMER_IF_BkUp_Read_SubSeconds */
  return ret;
}

/* USER CODE BEGIN EF */
uint32_t TIMER_IF_GetMonotonicMs(void)
{
  return GetMonotonicTicks();
}

bool TIMER_IF_RtcWriteBegin(void)
{
  uint32_t day_ms;
  bool     shift_pending;
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  ReadClock(&day_ms, &shift_pending);
  (void)DayMsClock_Update(&s_mono_clock, day_ms, shift_pending);
  s_mono_held = 1u;
  __set_PRIMASK(primask);
  return !shift_pending;
}

void TIMER_IF_RtcWriteEnd(bool calendar_set, int32_t shift_ms)
{
  uint32_t day_ms;
  bool     shift_pending;
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  ReadClock(&day_ms, &shift_pending);
  if (calendar_set)
  {
    DayMsClock_OnCalendarSet(&s_mono_clock, day_ms, shift_pending, shift_ms);
  }
  else
  {
    (void)DayMsClock_OnShift(&s_mono_clock, day_ms, shift_pending, shift_ms);
  }
  s_mono_held = 0u;
  __set_PRIMASK(primask);
}

void HAL_RTCEx_WakeUpTimerEventCallback(RTC_HandleTypeDef *hrtc)
{
  UTIL_TIMER_IRQ_MAP_PROCESS();
}

void RTC_GetCalendarBcd(FrameEpoch_t *out)
{
  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};
  hrtc.IsEnabled.RtcFeatures = UINT32_MAX;
  HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BCD);
  HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BCD);
  out->hours      = sTime.Hours;
  out->minutes    = sTime.Minutes;
  out->seconds    = sTime.Seconds;
  out->subseconds = sTime.SubSeconds; /* raw SSR, binary downcounter */
  out->day        = sDate.Date;
  out->month      = sDate.Month;
  out->year       = sDate.Year;
}

uint32_t TIMER_IF_GetDayTicks(bool at_edge)
{
  /* Direct register reads (bypass shadow, set in Init): a few microseconds,
   * against hundreds for HAL_RTC_GetTime at 4 MHz. SSR is read on both sides
   * of TR and the triple retried if a tick passed in between, so a second
   * rolling over (SSR 0 -> PREDIV_S, TR + 1) is never read half-way. */
  if (at_edge)
  {
    uint32_t ssr0 = READ_REG(RTC->SSR);
    while (READ_REG(RTC->SSR) == ssr0)
    {
    }
  }
  uint32_t ssr;
  uint32_t tr;
  do
  {
    ssr = READ_REG(RTC->SSR);
    tr  = READ_REG(RTC->TR);
  } while (READ_REG(RTC->SSR) != ssr);

  uint32_t h = ((tr & RTC_TR_HT) >> RTC_TR_HT_Pos) * 10u + ((tr & RTC_TR_HU) >> RTC_TR_HU_Pos);
  uint32_t m = ((tr & RTC_TR_MNT) >> RTC_TR_MNT_Pos) * 10u + ((tr & RTC_TR_MNU) >> RTC_TR_MNU_Pos);
  uint32_t sec = ((tr & RTC_TR_ST) >> RTC_TR_ST_Pos) * 10u + ((tr & RTC_TR_SU) >> RTC_TR_SU_Pos);
  /* SSR above PREDIV_S right after a SHIFTR advance: the calendar already
   * shows the next second, borrowed back here as in SubSecondsToMs. */
  int32_t ticks = (int32_t)((h * 3600u + m * 60u + sec) * (RTC_PREDIV_S + 1u))
                + (int32_t)RTC_PREDIV_S - (int32_t)ssr;
  if (ticks < 0)
  {
    ticks += (int32_t)(86400u * (RTC_PREDIV_S + 1u));
  }
  return (uint32_t)ticks;
}
/* USER CODE END EF */

/* Private functions ---------------------------------------------------------*/
/* USER CODE BEGIN PrFD */
static inline uint32_t GetTimerTicks(void)
{
  /* RTC calendar keeps running in STOP2. HAL_RTCEx_EnableBypassShadow (called in Init)
   * ensures direct register reads without RSF wait. GetDate must follow GetTime. */
  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};
  hrtc.IsEnabled.RtcFeatures = UINT32_MAX;
  HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
  HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
  int32_t ms = (int32_t)((sTime.Hours * 3600u + sTime.Minutes * 60u + sTime.Seconds) * 1000u)
             + SubSecondsToMs(sTime.SubSeconds);
  if (ms < 0)
  {
    ms += 86400000;  /* 00:00:00 read right after an ADD1S shift: still 23:59:59 */
  }
  return (uint32_t)ms;
}

/* Monotonic ms: the RTC time of day extended across midnight, RTC writes
 * excluded. Updated from the radio ISR, the RTC ISRs and thread context,
 * hence the critical section. Every UTIL_TIMER operation and HAL_GetTick
 * call updates it, far more often than the 12 h DayMsClock_Update needs.
 * During an RTC write it holds its value (HAL loops inside the write, and
 * any ISR, see time stand still for well under a millisecond). Holding,
 * rather than reading the RTC mid-write, keeps it from ever going back; the
 * cost is that a HAL RTC wait inside the write (INITF, set by hardware within
 * two RTCCLK cycles) cannot time out while held. */
static uint32_t GetMonotonicTicks(void)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  if (s_mono_held == 0u)
  {
    uint32_t day_ms;
    bool     shift_pending;
    ReadClock(&day_ms, &shift_pending);
    (void)DayMsClock_Update(&s_mono_clock, day_ms, shift_pending);
  }
  uint32_t mono = s_mono_clock.mono_ms;
  __set_PRIMASK(primask);
  return mono;
}

/* Time of day and SHPF read together: SHPF is sampled before and after the
 * time, and the pair is retried if a shift completed in between, so the
 * flag always tells whether the reading includes a written shift. */
static void ReadClock(uint32_t *day_ms, bool *shift_pending)
{
  uint32_t before;
  uint32_t after;
  do
  {
    before  = READ_BIT(RTC->ICSR, RTC_ICSR_SHPF);
    *day_ms = GetTimerTicks();
    after   = READ_BIT(RTC->ICSR, RTC_ICSR_SHPF);
  } while (before != after);
  *shift_pending = (after != 0u);
}

/* Milliseconds elapsed in the current calendar second. SSR counts down from
 * PREDIV_S, but a SHIFTR advance (ADD1S=1, SUBFS) adds SUBFS to it: for up to
 * one second SSR > PREDIV_S while the calendar already shows the next second.
 * The result is then negative (floored), borrowing from that second. */
static inline int32_t SubSecondsToMs(uint32_t ssr)
{
  if (ssr <= RTC_PREDIV_S)
  {
    return (int32_t)(((RTC_PREDIV_S - ssr) * 1000u) / (RTC_PREDIV_S + 1u));
  }
  return -(int32_t)(((ssr - RTC_PREDIV_S) * 1000u + RTC_PREDIV_S) / (RTC_PREDIV_S + 1u));
}
/* USER CODE END PrFD */
