################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../SubGHz_Phy/Logic/compliance_engine.c \
../SubGHz_Phy/Logic/freq_resolver.c \
../SubGHz_Phy/Logic/mac_state_machine_c2.c \
../SubGHz_Phy/Logic/tdma_machine.c 

OBJS += \
./SubGHz_Phy/Logic/compliance_engine.o \
./SubGHz_Phy/Logic/freq_resolver.o \
./SubGHz_Phy/Logic/mac_state_machine_c2.o \
./SubGHz_Phy/Logic/tdma_machine.o 

C_DEPS += \
./SubGHz_Phy/Logic/compliance_engine.d \
./SubGHz_Phy/Logic/freq_resolver.d \
./SubGHz_Phy/Logic/mac_state_machine_c2.d \
./SubGHz_Phy/Logic/tdma_machine.d 


# Each subdirectory must supply rules for building sources it contributes
SubGHz_Phy/Logic/%.o SubGHz_Phy/Logic/%.su SubGHz_Phy/Logic/%.cyclo: ../SubGHz_Phy/Logic/%.c SubGHz_Phy/Logic/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -g3 -DDEBUG -DNODE_CLASS=NODE_CLASS_C2 -DCORE_CM0PLUS -DUSE_HAL_DRIVER -DSTM32WL55xx -c -I../Core/Inc -I../SubGHz_Phy/App -I../SubGHz_Phy/Logic -I../SubGHz_Phy/Target -I../MbMux -I../../Common/MbMux -I../../Utilities/trace/adv_trace -I../../Drivers/STM32WLxx_HAL_Driver/Inc -I../../Drivers/STM32WLxx_HAL_Driver/Inc/Legacy -I../../Utilities/misc -I../../Utilities/sequencer -I../../Utilities/timer -I../../Utilities/lpm/tiny_lpm -I../../Drivers/CMSIS/Device/ST/STM32WLxx/Include -I../../Middlewares/Third_Party/SubGHz_Phy/radio_driver -I../../Drivers/CMSIS/Include -I../../Common/Protocol -I../../Common/SharedMemory -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-SubGHz_Phy-2f-Logic

clean-SubGHz_Phy-2f-Logic:
	-$(RM) ./SubGHz_Phy/Logic/compliance_engine.cyclo ./SubGHz_Phy/Logic/compliance_engine.d ./SubGHz_Phy/Logic/compliance_engine.o ./SubGHz_Phy/Logic/compliance_engine.su ./SubGHz_Phy/Logic/freq_resolver.cyclo ./SubGHz_Phy/Logic/freq_resolver.d ./SubGHz_Phy/Logic/freq_resolver.o ./SubGHz_Phy/Logic/freq_resolver.su ./SubGHz_Phy/Logic/mac_state_machine_c2.cyclo ./SubGHz_Phy/Logic/mac_state_machine_c2.d ./SubGHz_Phy/Logic/mac_state_machine_c2.o ./SubGHz_Phy/Logic/mac_state_machine_c2.su ./SubGHz_Phy/Logic/tdma_machine.cyclo ./SubGHz_Phy/Logic/tdma_machine.d ./SubGHz_Phy/Logic/tdma_machine.o ./SubGHz_Phy/Logic/tdma_machine.su

.PHONY: clean-SubGHz_Phy-2f-Logic

