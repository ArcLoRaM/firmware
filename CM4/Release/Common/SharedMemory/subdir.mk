################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
C:/Users/Simon/Desktop/ArcLoram/ArcLoRaM_Base/Common/SharedMemory/shared_mem.c 

OBJS += \
./Common/SharedMemory/shared_mem.o 

C_DEPS += \
./Common/SharedMemory/shared_mem.d 


# Each subdirectory must supply rules for building sources it contributes
Common/SharedMemory/shared_mem.o: C:/Users/Simon/Desktop/ArcLoram/ArcLoRaM_Base/Common/SharedMemory/shared_mem.c Common/SharedMemory/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -DCORE_CM4 -DUSE_HAL_DRIVER -DSTM32WL55xx -c -I../Core/Inc -I../SubGHz_Phy/App -I../MbMux -I../../Common/MbMux -I../../Utilities/trace/adv_trace -I../../Drivers/STM32WLxx_HAL_Driver/Inc -I../../Drivers/STM32WLxx_HAL_Driver/Inc/Legacy -I../../Utilities/misc -I../../Utilities/sequencer -I../../Utilities/timer -I../../Utilities/lpm/tiny_lpm -I../../Drivers/CMSIS/Device/ST/STM32WLxx/Include -I../../Drivers/CMSIS/Include -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Common-2f-SharedMemory

clean-Common-2f-SharedMemory:
	-$(RM) ./Common/SharedMemory/shared_mem.cyclo ./Common/SharedMemory/shared_mem.d ./Common/SharedMemory/shared_mem.o ./Common/SharedMemory/shared_mem.su

.PHONY: clean-Common-2f-SharedMemory

