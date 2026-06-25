################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../SubGHz_Phy/Logic/cm4_diag.c 

OBJS += \
./SubGHz_Phy/Logic/cm4_diag.o 

C_DEPS += \
./SubGHz_Phy/Logic/cm4_diag.d 


# Each subdirectory must supply rules for building sources it contributes
SubGHz_Phy/Logic/%.o SubGHz_Phy/Logic/%.su SubGHz_Phy/Logic/%.cyclo: ../SubGHz_Phy/Logic/%.c SubGHz_Phy/Logic/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -DCORE_CM4 -DUSE_HAL_DRIVER -DSTM32WL55xx -c -I../Core/Inc -I../SubGHz_Phy/App -I../MbMux -I../../Common/MbMux -I../../Utilities/trace/adv_trace -I../../Drivers/STM32WLxx_HAL_Driver/Inc -I../../Drivers/STM32WLxx_HAL_Driver/Inc/Legacy -I../../Utilities/misc -I../../Utilities/sequencer -I../../Utilities/timer -I../../Utilities/lpm/tiny_lpm -I../../Drivers/CMSIS/Device/ST/STM32WLxx/Include -I../../Drivers/CMSIS/Include -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-SubGHz_Phy-2f-Logic

clean-SubGHz_Phy-2f-Logic:
	-$(RM) ./SubGHz_Phy/Logic/cm4_diag.cyclo ./SubGHz_Phy/Logic/cm4_diag.d ./SubGHz_Phy/Logic/cm4_diag.o ./SubGHz_Phy/Logic/cm4_diag.su

.PHONY: clean-SubGHz_Phy-2f-Logic

