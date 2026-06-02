################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../MbMux/features_info.c \
../MbMux/mbmux.c \
../MbMux/mbmuxif_radio.c \
../MbMux/mbmuxif_sys.c \
../MbMux/mbmuxif_trace.c \
../MbMux/radio_mbwrapper.c 

OBJS += \
./MbMux/features_info.o \
./MbMux/mbmux.o \
./MbMux/mbmuxif_radio.o \
./MbMux/mbmuxif_sys.o \
./MbMux/mbmuxif_trace.o \
./MbMux/radio_mbwrapper.o 

C_DEPS += \
./MbMux/features_info.d \
./MbMux/mbmux.d \
./MbMux/mbmuxif_radio.d \
./MbMux/mbmuxif_sys.d \
./MbMux/mbmuxif_trace.d \
./MbMux/radio_mbwrapper.d 


# Each subdirectory must supply rules for building sources it contributes
MbMux/%.o MbMux/%.su MbMux/%.cyclo: ../MbMux/%.c MbMux/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -g3 -DDEBUG -DNODE_CLASS=NODE_CLASS_C3 -DCORE_CM0PLUS -DUSE_HAL_DRIVER -DSTM32WL55xx -c -I../Core/Inc -I../SubGHz_Phy/App -I../SubGHz_Phy/Target -I../SubGHz_Phy/Logic -I../MbMux -I../../Common/MbMux -I../../Utilities/trace/adv_trace -I../../Drivers/STM32WLxx_HAL_Driver/Inc -I../../Drivers/STM32WLxx_HAL_Driver/Inc/Legacy -I../../Utilities/misc -I../../Utilities/sequencer -I../../Utilities/timer -I../../Utilities/lpm/tiny_lpm -I../../Drivers/CMSIS/Device/ST/STM32WLxx/Include -I../../Middlewares/Third_Party/SubGHz_Phy/radio_driver -I../../Drivers/CMSIS/Include -I../../Common/Protocol -I../../Common/SharedMemory -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-MbMux

clean-MbMux:
	-$(RM) ./MbMux/features_info.cyclo ./MbMux/features_info.d ./MbMux/features_info.o ./MbMux/features_info.su ./MbMux/mbmux.cyclo ./MbMux/mbmux.d ./MbMux/mbmux.o ./MbMux/mbmux.su ./MbMux/mbmuxif_radio.cyclo ./MbMux/mbmuxif_radio.d ./MbMux/mbmuxif_radio.o ./MbMux/mbmuxif_radio.su ./MbMux/mbmuxif_sys.cyclo ./MbMux/mbmuxif_sys.d ./MbMux/mbmuxif_sys.o ./MbMux/mbmuxif_sys.su ./MbMux/mbmuxif_trace.cyclo ./MbMux/mbmuxif_trace.d ./MbMux/mbmuxif_trace.o ./MbMux/mbmuxif_trace.su ./MbMux/radio_mbwrapper.cyclo ./MbMux/radio_mbwrapper.d ./MbMux/radio_mbwrapper.o ./MbMux/radio_mbwrapper.su

.PHONY: clean-MbMux

