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
#include "lptim.h"  /* hlptim1 handle */
#include "main.h"   /* Error_Handler, RTC_PREDIV_S */
#include "rtc.h"    /* hrtc — used by TIMER_IF_GetTime */
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
#define MIN_ALARM_DELAY  3u        /* minimum LPTIM ticks before firing */
#define LPTIM_CLOCK_HZ   1024u     /* LSE 32768 Hz / DIV32 */
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
static volatile uint8_t lptim1_running = 0u;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
static inline uint32_t GetTimerTicks(void);
/* USER CODE END PFP */

/* Exported functions ---------------------------------------------------------*/
UTIL_TIMER_Status_t TIMER_IF_Init(void)
{
  UTIL_TIMER_Status_t ret = UTIL_TIMER_OK;
  /* USER CODE BEGIN TIMER_IF_Init */
  if (RTC_Initialized == 0)
  {
    /* IER must be written while LPTIM is disabled (MX_LPTIM1_Init leaves it disabled).
     * CMPM is permanently enabled; actual dispatch is gated by lptim1_running. */
    hlptim1.Instance->IER = LPTIM_IT_CMPM;

    /* Enable LPTIM to write shadow registers */
    hlptim1.Instance->CR |= LPTIM_CR_ENABLE;

    /* ARR = 0xFFFF — free-running ~64 s period at 1024 Hz */
    __HAL_LPTIM_CLEAR_FLAG(&hlptim1, LPTIM_FLAG_ARROK);
    __HAL_LPTIM_AUTORELOAD_SET(&hlptim1, 0xFFFFu);
    while (!__HAL_LPTIM_GET_FLAG(&hlptim1, LPTIM_FLAG_ARROK)) {}
    __HAL_LPTIM_CLEAR_FLAG(&hlptim1, LPTIM_FLAG_ARROK);

    /* CMP = 0xFFFF — harmless until first StartTimer */
    __HAL_LPTIM_CLEAR_FLAG(&hlptim1, LPTIM_FLAG_CMPOK);
    __HAL_LPTIM_COMPARE_SET(&hlptim1, 0xFFFFu);
    while (!__HAL_LPTIM_GET_FLAG(&hlptim1, LPTIM_FLAG_CMPOK)) {}
    __HAL_LPTIM_CLEAR_FLAG(&hlptim1, LPTIM_FLAG_CMPOK);

    hlptim1.Instance->CR |= LPTIM_CR_CNTSTRT;
    hlptim1.State = HAL_LPTIM_STATE_READY;

    RtcTimerContext = GetTimerTicks();
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

  uint32_t now          = GetTimerTicks();
  uint32_t remaining_ms = timeout - now;               /* unsigned; correct when context is fresh */
  if (remaining_ms > 64000u) { remaining_ms = 1u; }   /* stale-context safety clamp (CNT wrap) */

  /* Convert ms to LPTIM 1024 Hz counts; clamp to 16-bit max (~64 s) */
  uint32_t count = (uint32_t)(((uint64_t)remaining_ms * LPTIM_CLOCK_HZ) / 1000u);
  if (count == 0u) { count = 1u; }
  if (count > 0xFFFFu) { count = 0xFFFFu; }

  /* Double-read CNT for stability across APB / LSE clock domains */
  uint32_t cnt;
  do { cnt = hlptim1.Instance->CNT; } while (cnt != hlptim1.Instance->CNT);

  /* Write CMP = target counter value; LPTIM running with stable LSE, no LPTIM_Disable */
  uint32_t cmp = (cnt + count) & 0xFFFFu;
  __HAL_LPTIM_CLEAR_FLAG(&hlptim1, LPTIM_FLAG_CMPOK);
  __HAL_LPTIM_COMPARE_SET(&hlptim1, cmp);
  uint32_t ts = GetTimerTicks();
  while (!__HAL_LPTIM_GET_FLAG(&hlptim1, LPTIM_FLAG_CMPOK))
  {
    if (GetTimerTicks() - ts > 10u) { Error_Handler(); break; }
  }
  __HAL_LPTIM_CLEAR_FLAG(&hlptim1, LPTIM_FLAG_CMPOK);

  /* Discard any match that fired during the CMPOK window; next match will fire at the correct future count. */
  NVIC_ClearPendingIRQ(LPTIM1_IRQn);
  __HAL_LPTIM_CLEAR_FLAG(&hlptim1, LPTIM_FLAG_CMPM);
  lptim1_running = 1u;
  /* USER CODE END TIMER_IF_StartTimer */
  return ret;
}

UTIL_TIMER_Status_t TIMER_IF_StopTimer(void)
{
  UTIL_TIMER_Status_t ret = UTIL_TIMER_OK;
  /* USER CODE BEGIN TIMER_IF_StopTimer */
  lptim1_running = 0u;  /* LPTIM keeps running; stale CMPM fires are ignored in callback */
  /* USER CODE END TIMER_IF_StopTimer */
  return ret;
}

uint32_t TIMER_IF_SetTimerContext(void)
{
  /* USER CODE BEGIN TIMER_IF_SetTimerContext */
  RtcTimerContext = GetTimerTicks();
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
  ret = (uint32_t)(GetTimerTicks() - RtcTimerContext);
  /* USER CODE END TIMER_IF_GetTimerElapsedTime */
  return ret;
}

uint32_t TIMER_IF_GetTimerValue(void)
{
  uint32_t ret = 0;
  /* USER CODE BEGIN TIMER_IF_GetTimerValue */
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

  /* USER CODE END TIMER_IF_DelayMs */
}

uint32_t TIMER_IF_GetTime(uint16_t *mSeconds)
{
  uint32_t seconds = 0;
  /* USER CODE BEGIN TIMER_IF_GetTime */
  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};

  /* HAL_RTC_GetDate must follow GetTime to unlock shadow registers */
  HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
  HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

  if (mSeconds != NULL)
  {
    *mSeconds = (uint16_t)(((RTC_PREDIV_S - sTime.SubSeconds) * 1000u) / (RTC_PREDIV_S + 1u));
  }

  struct tm t = {0};
  t.tm_year  = sDate.Year + 100;  /* RTC year 0-99 → years since 1900 */
  t.tm_mon   = sDate.Month - 1;   /* RTC month 1-12 → tm_mon 0-11 */
  t.tm_mday  = sDate.Date;
  t.tm_hour  = sTime.Hours;
  t.tm_min   = sTime.Minutes;
  t.tm_sec   = sTime.Seconds;
  t.tm_isdst = -1;

  seconds = (uint32_t)mktime(&t);
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
void HAL_LPTIM_CompareMatchCallback(LPTIM_HandleTypeDef *hlptim)
{
  if (lptim1_running)
  {
    lptim1_running = 0u;
    UTIL_TIMER_IRQ_MAP_PROCESS();
  }
}
/* USER CODE END EF */

/* Private functions ---------------------------------------------------------*/
/* USER CODE BEGIN PrFD */
static inline uint32_t GetTimerTicks(void)
{
  /* LPTIM1 CNT keeps running in STOP2 (LSI-clocked). Double-read guards APB/LSI crossing.
   * Returns 0-63999 ms, wrapping every ~64 s. */
  uint32_t cnt;
  do { cnt = hlptim1.Instance->CNT; } while (cnt != hlptim1.Instance->CNT);
  return (cnt * 125u) >> 7;  /* cnt * 1000 / 1024 */
}
/* USER CODE END PrFD */
