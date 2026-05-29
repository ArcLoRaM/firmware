/* Tests/stubs/stm32wlxx_hal_conf.h
 *
 * Intentionally empty. The real stm32wlxx_hal_conf.h conditionally includes
 * every HAL module header (RTC, GPIO, UART, …). On the host we do not want
 * any of those — each module that Logic code actually uses gets its own
 * targeted stub (stm32wlxx_hal_rtc.h, etc.) instead. */

#ifndef STM32WLxx_HAL_CONF_H
#define STM32WLxx_HAL_CONF_H
#endif /* STM32WLxx_HAL_CONF_H */
