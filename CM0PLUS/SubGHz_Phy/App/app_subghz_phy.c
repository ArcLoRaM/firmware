/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    app_subghz_phy.c
  * @author  MCD Application Team
  * @brief   Application of the SubGHz_Phy Middleware
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
#include "app_subghz_phy.h"
#include "sys_app.h"
#include "stm32_seq.h"

/* USER CODE BEGIN Includes */
#ifdef USING_NUCLEO
#include "gpio.h"
#include "stm32_timer.h"
#endif
/* USER CODE END Includes */

/* External variables ---------------------------------------------------------*/
/* USER CODE BEGIN EV */

/* USER CODE END EV */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#ifdef USING_NUCLEO
#define LED_CM0_BLINK_PERIOD_MS  2000U
#endif
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
#ifdef USING_NUCLEO
static UTIL_TIMER_Object_t LedTimer;
#endif
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
#ifdef USING_NUCLEO
static void LedToggle_Cb(void *arg);
#endif
/* USER CODE END PFP */

/* Exported functions --------------------------------------------------------*/

void MX_SubGHz_Phy_Init(void)
{
  /* USER CODE BEGIN MX_SubGHz_Phy_Init_1 */

  /* USER CODE END MX_SubGHz_Phy_Init_1 */
  SystemApp_Init();
  /* USER CODE BEGIN MX_SubGHz_Phy_Init_1_1 */

  /* USER CODE END MX_SubGHz_Phy_Init_1_1 */
  /* USER CODE BEGIN MX_SubGHz_Phy_Init_2 */
#ifdef USING_NUCLEO
  BSP_LED_GPIO_Init();
  UTIL_TIMER_Create(&LedTimer, LED_CM0_BLINK_PERIOD_MS, UTIL_TIMER_PERIODIC, LedToggle_Cb, NULL);
  UTIL_TIMER_Start(&LedTimer);
#endif
  /* USER CODE END MX_SubGHz_Phy_Init_2 */
}

void MX_SubGHz_Phy_Process(void)
{
  /* USER CODE BEGIN MX_SubGHz_Phy_Process_1 */

  /* USER CODE END MX_SubGHz_Phy_Process_1 */
  UTIL_SEQ_Run(UTIL_SEQ_DEFAULT);
  /* USER CODE BEGIN MX_SubGHz_Phy_Process_2 */

  /* USER CODE END MX_SubGHz_Phy_Process_2 */
}

/* USER CODE BEGIN EF */
#ifdef USING_NUCLEO
static void LedToggle_Cb(void *arg)
{
  HAL_GPIO_TogglePin(LED_BLINK_CM0_Port, LED_BLINK_CM0_Pin);
    APP_LOG(TS_ON, VLEVEL_H, "Hello Celium from CM0PLUS\r\n");

}
#endif
/* USER CODE END EF */

/* Private Functions Definition -----------------------------------------------*/
/* USER CODE BEGIN PrFD */

/* USER CODE END PrFD */
