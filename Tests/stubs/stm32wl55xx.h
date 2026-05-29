/* Tests/stubs/stm32wl55xx.h
 *
 * Empty device register map stub. The real stm32wl55xx.h defines every
 * peripheral register struct (RTC_TypeDef, LPTIM_TypeDef, …) as packed
 * structs mapped to fixed hardware addresses. On a host PC, none of that
 * exists. Logic code must never dereference hardware addresses directly —
 * if it does, the test will crash or segfault, which is the correct signal
 * that the code needs a hardware abstraction layer. */

#ifndef STM32WL55xx_H
#define STM32WL55xx_H
#endif /* STM32WL55xx_H */
