################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
C:/Users/Simon/Desktop/ArcLoram/ArcLoRaM_Base/Common/Protocol/tdma_table.c 

OBJS += \
./Common/Protocol/tdma_table.o 

C_DEPS += \
./Common/Protocol/tdma_table.d 


# Each subdirectory must supply rules for building sources it contributes
Common/Protocol/tdma_table.o: C:/Users/Simon/Desktop/ArcLoram/ArcLoRaM_Base/Common/Protocol/tdma_table.c Common/Protocol/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -g3 -DDEBUG -DNODE_CLASS=NODE_CLASS_C3 -DCORE_CM0PLUS -DUSE_HAL_DRIVER -DSTM32WL55xx -c -I../Core/Inc -I../SubGHz_Phy/App -I../SubGHz_Phy/Target -I../SubGHz_Phy/Logic -I../MbMux -I../../Common/MbMux -I../../Utilities/trace/adv_trace -I../../Drivers/STM32WLxx_HAL_Driver/Inc -I../../Drivers/STM32WLxx_HAL_Driver/Inc/Legacy -I../../Utilities/misc -I../../Utilities/sequencer -I../../Utilities/timer -I../../Utilities/lpm/tiny_lpm -I../../Drivers/CMSIS/Device/ST/STM32WLxx/Include -I../../Middlewares/Third_Party/SubGHz_Phy/radio_driver -I../../Drivers/CMSIS/Include -I../../Common/Protocol -I../../Common/SharedMemory -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Common-2f-Protocol

clean-Common-2f-Protocol:
	-$(RM) ./Common/Protocol/tdma_table.cyclo ./Common/Protocol/tdma_table.d ./Common/Protocol/tdma_table.o ./Common/Protocol/tdma_table.su

.PHONY: clean-Common-2f-Protocol

