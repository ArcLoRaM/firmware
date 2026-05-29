/* Tests/stubs/stm32wlxx_hal_gpio.h
 *
 * Minimal GPIO stub. Add pin constants as your Logic code references them. */

#ifndef STM32WLxx_HAL_GPIO_H
#define STM32WLxx_HAL_GPIO_H

#include "stm32wlxx_hal_def.h"

typedef enum {
    GPIO_PIN_RESET = 0,
    GPIO_PIN_SET
} GPIO_PinState;

/* Opaque peripheral pointer — Logic code should never dereference this */
typedef uint32_t GPIO_TypeDef;

#define GPIO_PIN_0  0x0001U
#define GPIO_PIN_1  0x0002U
#define GPIO_PIN_2  0x0004U
#define GPIO_PIN_3  0x0008U
#define GPIO_PIN_4  0x0010U
#define GPIO_PIN_5  0x0020U
#define GPIO_PIN_6  0x0040U
#define GPIO_PIN_7  0x0080U
#define GPIO_PIN_8  0x0100U
#define GPIO_PIN_15 0x8000U

static inline GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
    { (void)GPIOx; (void)GPIO_Pin; return GPIO_PIN_RESET; }

static inline void HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState)
    { (void)GPIOx; (void)GPIO_Pin; (void)PinState; }

static inline void HAL_GPIO_TogglePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
    { (void)GPIOx; (void)GPIO_Pin; }

#endif /* STM32WLxx_HAL_GPIO_H */
