################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
C:/Users/Simon/Desktop/ArcLoram/ArcLoRaM_Base/Middlewares/Third_Party/SubGHz_Phy/radio_driver/lr_fhss_mac.c \
C:/Users/Simon/Desktop/ArcLoram/ArcLoRaM_Base/Middlewares/Third_Party/SubGHz_Phy/radio_driver/radio.c \
C:/Users/Simon/Desktop/ArcLoram/ArcLoRaM_Base/Middlewares/Third_Party/SubGHz_Phy/radio_driver/radio_driver.c \
C:/Users/Simon/Desktop/ArcLoram/ArcLoRaM_Base/Middlewares/Third_Party/SubGHz_Phy/radio_driver/radio_fw.c \
C:/Users/Simon/Desktop/ArcLoram/ArcLoRaM_Base/Middlewares/Third_Party/SubGHz_Phy/radio_driver/wl_lr_fhss.c 

OBJS += \
./Middlewares/Third_Party/SubGHz_Phy/lr_fhss_mac.o \
./Middlewares/Third_Party/SubGHz_Phy/radio.o \
./Middlewares/Third_Party/SubGHz_Phy/radio_driver.o \
./Middlewares/Third_Party/SubGHz_Phy/radio_fw.o \
./Middlewares/Third_Party/SubGHz_Phy/wl_lr_fhss.o 

C_DEPS += \
./Middlewares/Third_Party/SubGHz_Phy/lr_fhss_mac.d \
./Middlewares/Third_Party/SubGHz_Phy/radio.d \
./Middlewares/Third_Party/SubGHz_Phy/radio_driver.d \
./Middlewares/Third_Party/SubGHz_Phy/radio_fw.d \
./Middlewares/Third_Party/SubGHz_Phy/wl_lr_fhss.d 


# Each subdirectory must supply rules for building sources it contributes
Middlewares/Third_Party/SubGHz_Phy/lr_fhss_mac.o: C:/Users/Simon/Desktop/ArcLoram/ArcLoRaM_Base/Middlewares/Third_Party/SubGHz_Phy/radio_driver/lr_fhss_mac.c Middlewares/Third_Party/SubGHz_Phy/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -DCORE_CM0PLUS -DUSE_HAL_DRIVER -DSTM32WL55xx -c -I../Core/Inc -I../SubGHz_Phy/App -I../SubGHz_Phy/Target -I../MbMux -I../../Common/MbMux -I../../Utilities/trace/adv_trace -I../../Drivers/STM32WLxx_HAL_Driver/Inc -I../../Drivers/STM32WLxx_HAL_Driver/Inc/Legacy -I../../Utilities/misc -I../../Utilities/sequencer -I../../Utilities/timer -I../../Utilities/lpm/tiny_lpm -I../../Drivers/CMSIS/Device/ST/STM32WLxx/Include -I../../Middlewares/Third_Party/SubGHz_Phy/radio_driver -I../../Drivers/CMSIS/Include -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"
Middlewares/Third_Party/SubGHz_Phy/radio.o: C:/Users/Simon/Desktop/ArcLoram/ArcLoRaM_Base/Middlewares/Third_Party/SubGHz_Phy/radio_driver/radio.c Middlewares/Third_Party/SubGHz_Phy/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -DCORE_CM0PLUS -DUSE_HAL_DRIVER -DSTM32WL55xx -c -I../Core/Inc -I../SubGHz_Phy/App -I../SubGHz_Phy/Target -I../MbMux -I../../Common/MbMux -I../../Utilities/trace/adv_trace -I../../Drivers/STM32WLxx_HAL_Driver/Inc -I../../Drivers/STM32WLxx_HAL_Driver/Inc/Legacy -I../../Utilities/misc -I../../Utilities/sequencer -I../../Utilities/timer -I../../Utilities/lpm/tiny_lpm -I../../Drivers/CMSIS/Device/ST/STM32WLxx/Include -I../../Middlewares/Third_Party/SubGHz_Phy/radio_driver -I../../Drivers/CMSIS/Include -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"
Middlewares/Third_Party/SubGHz_Phy/radio_driver.o: C:/Users/Simon/Desktop/ArcLoram/ArcLoRaM_Base/Middlewares/Third_Party/SubGHz_Phy/radio_driver/radio_driver.c Middlewares/Third_Party/SubGHz_Phy/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -DCORE_CM0PLUS -DUSE_HAL_DRIVER -DSTM32WL55xx -c -I../Core/Inc -I../SubGHz_Phy/App -I../SubGHz_Phy/Target -I../MbMux -I../../Common/MbMux -I../../Utilities/trace/adv_trace -I../../Drivers/STM32WLxx_HAL_Driver/Inc -I../../Drivers/STM32WLxx_HAL_Driver/Inc/Legacy -I../../Utilities/misc -I../../Utilities/sequencer -I../../Utilities/timer -I../../Utilities/lpm/tiny_lpm -I../../Drivers/CMSIS/Device/ST/STM32WLxx/Include -I../../Middlewares/Third_Party/SubGHz_Phy/radio_driver -I../../Drivers/CMSIS/Include -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"
Middlewares/Third_Party/SubGHz_Phy/radio_fw.o: C:/Users/Simon/Desktop/ArcLoram/ArcLoRaM_Base/Middlewares/Third_Party/SubGHz_Phy/radio_driver/radio_fw.c Middlewares/Third_Party/SubGHz_Phy/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -DCORE_CM0PLUS -DUSE_HAL_DRIVER -DSTM32WL55xx -c -I../Core/Inc -I../SubGHz_Phy/App -I../SubGHz_Phy/Target -I../MbMux -I../../Common/MbMux -I../../Utilities/trace/adv_trace -I../../Drivers/STM32WLxx_HAL_Driver/Inc -I../../Drivers/STM32WLxx_HAL_Driver/Inc/Legacy -I../../Utilities/misc -I../../Utilities/sequencer -I../../Utilities/timer -I../../Utilities/lpm/tiny_lpm -I../../Drivers/CMSIS/Device/ST/STM32WLxx/Include -I../../Middlewares/Third_Party/SubGHz_Phy/radio_driver -I../../Drivers/CMSIS/Include -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"
Middlewares/Third_Party/SubGHz_Phy/wl_lr_fhss.o: C:/Users/Simon/Desktop/ArcLoram/ArcLoRaM_Base/Middlewares/Third_Party/SubGHz_Phy/radio_driver/wl_lr_fhss.c Middlewares/Third_Party/SubGHz_Phy/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -DCORE_CM0PLUS -DUSE_HAL_DRIVER -DSTM32WL55xx -c -I../Core/Inc -I../SubGHz_Phy/App -I../SubGHz_Phy/Target -I../MbMux -I../../Common/MbMux -I../../Utilities/trace/adv_trace -I../../Drivers/STM32WLxx_HAL_Driver/Inc -I../../Drivers/STM32WLxx_HAL_Driver/Inc/Legacy -I../../Utilities/misc -I../../Utilities/sequencer -I../../Utilities/timer -I../../Utilities/lpm/tiny_lpm -I../../Drivers/CMSIS/Device/ST/STM32WLxx/Include -I../../Middlewares/Third_Party/SubGHz_Phy/radio_driver -I../../Drivers/CMSIS/Include -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Middlewares-2f-Third_Party-2f-SubGHz_Phy

clean-Middlewares-2f-Third_Party-2f-SubGHz_Phy:
	-$(RM) ./Middlewares/Third_Party/SubGHz_Phy/lr_fhss_mac.cyclo ./Middlewares/Third_Party/SubGHz_Phy/lr_fhss_mac.d ./Middlewares/Third_Party/SubGHz_Phy/lr_fhss_mac.o ./Middlewares/Third_Party/SubGHz_Phy/lr_fhss_mac.su ./Middlewares/Third_Party/SubGHz_Phy/radio.cyclo ./Middlewares/Third_Party/SubGHz_Phy/radio.d ./Middlewares/Third_Party/SubGHz_Phy/radio.o ./Middlewares/Third_Party/SubGHz_Phy/radio.su ./Middlewares/Third_Party/SubGHz_Phy/radio_driver.cyclo ./Middlewares/Third_Party/SubGHz_Phy/radio_driver.d ./Middlewares/Third_Party/SubGHz_Phy/radio_driver.o ./Middlewares/Third_Party/SubGHz_Phy/radio_driver.su ./Middlewares/Third_Party/SubGHz_Phy/radio_fw.cyclo ./Middlewares/Third_Party/SubGHz_Phy/radio_fw.d ./Middlewares/Third_Party/SubGHz_Phy/radio_fw.o ./Middlewares/Third_Party/SubGHz_Phy/radio_fw.su ./Middlewares/Third_Party/SubGHz_Phy/wl_lr_fhss.cyclo ./Middlewares/Third_Party/SubGHz_Phy/wl_lr_fhss.d ./Middlewares/Third_Party/SubGHz_Phy/wl_lr_fhss.o ./Middlewares/Third_Party/SubGHz_Phy/wl_lr_fhss.su

.PHONY: clean-Middlewares-2f-Third_Party-2f-SubGHz_Phy

