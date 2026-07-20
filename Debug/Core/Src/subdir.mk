################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
S_SRCS += \
../Core/Src/tx_initialize_low_level.s 

C_SRCS += \
../Core/Src/app_bme280.c \
../Core/Src/app_guix.c \
../Core/Src/app_state.c \
../Core/Src/app_threadx.c \
../Core/Src/app_vnc_frame.c \
../Core/Src/bme280_sensor.c \
../Core/Src/debug_log.c \
../Core/Src/dma2d.c \
../Core/Src/eth.c \
../Core/Src/fmc.c \
../Core/Src/gpio.c \
../Core/Src/i2c.c \
../Core/Src/ltdc.c \
../Core/Src/main.c \
../Core/Src/stm32f7xx_hal_msp.c \
../Core/Src/stm32f7xx_hal_timebase_tim.c \
../Core/Src/stm32f7xx_it.c \
../Core/Src/syscalls.c \
../Core/Src/sysmem.c \
../Core/Src/system_stm32f7xx.c \
../Core/Src/usb_otg.c 

OBJS += \
./Core/Src/app_bme280.o \
./Core/Src/app_guix.o \
./Core/Src/app_state.o \
./Core/Src/app_threadx.o \
./Core/Src/app_vnc_frame.o \
./Core/Src/bme280_sensor.o \
./Core/Src/debug_log.o \
./Core/Src/dma2d.o \
./Core/Src/eth.o \
./Core/Src/fmc.o \
./Core/Src/gpio.o \
./Core/Src/i2c.o \
./Core/Src/ltdc.o \
./Core/Src/main.o \
./Core/Src/stm32f7xx_hal_msp.o \
./Core/Src/stm32f7xx_hal_timebase_tim.o \
./Core/Src/stm32f7xx_it.o \
./Core/Src/syscalls.o \
./Core/Src/sysmem.o \
./Core/Src/system_stm32f7xx.o \
./Core/Src/tx_initialize_low_level.o \
./Core/Src/usb_otg.o 

S_DEPS += \
./Core/Src/tx_initialize_low_level.d 

C_DEPS += \
./Core/Src/app_bme280.d \
./Core/Src/app_guix.d \
./Core/Src/app_state.d \
./Core/Src/app_threadx.d \
./Core/Src/app_vnc_frame.d \
./Core/Src/bme280_sensor.d \
./Core/Src/debug_log.d \
./Core/Src/dma2d.d \
./Core/Src/eth.d \
./Core/Src/fmc.d \
./Core/Src/gpio.d \
./Core/Src/i2c.d \
./Core/Src/ltdc.d \
./Core/Src/main.d \
./Core/Src/stm32f7xx_hal_msp.d \
./Core/Src/stm32f7xx_hal_timebase_tim.d \
./Core/Src/stm32f7xx_it.d \
./Core/Src/syscalls.d \
./Core/Src/sysmem.d \
./Core/Src/system_stm32f7xx.d \
./Core/Src/usb_otg.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/%.o Core/Src/%.su Core/Src/%.cyclo: ../Core/Src/%.c Core/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DTX_INCLUDE_USER_DEFINE_FILE -DUX_INCLUDE_USER_DEFINE_FILE -DNX_INCLUDE_USER_DEFINE_FILE -DGX_INCLUDE_USER_DEFINE_FILE -DUSE_HAL_DRIVER -DSTM32F746xx -c -I../Core/Inc -I../AZURE_RTOS/App -I../USBX/App -I../USBX/Target -I../NetXDuo/App -I../NetXDuo/Target -I../Drivers/STM32F7xx_HAL_Driver/Inc -I../Drivers/STM32F7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F7xx/Include -I../Drivers/CMSIS/Include -I../Drivers/BSP/Components/lan8742/ -I../Middlewares/ST/netxduo/common/drivers/ethernet/ -I../Middlewares/ST/usbx/common/core/inc/ -I../Middlewares/ST/usbx/ports/generic/inc/ -I../Middlewares/ST/usbx/common/usbx_stm32_device_controllers/ -I../Middlewares/ST/netxduo/addons/dhcp/ -I../Middlewares/ST/netxduo/common/inc/ -I../Middlewares/ST/netxduo/ports/cortex_m7/gnu/inc/ -I../Middlewares/ST/threadx/common/inc/ -I../Middlewares/ST/threadx/ports/cortex_m7/gnu/inc/ -I../Middlewares/ST/guix/common/inc/ -I../Middlewares/ST/guix/ports/cortex_m7/gnu/inc/ -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -o "$@"
Core/Src/%.o: ../Core/Src/%.s Core/Src/subdir.mk
	arm-none-eabi-gcc -mcpu=cortex-m7 -g3 -DDEBUG -c -I../Core/Inc -I../AZURE_RTOS/App -I../USBX/App -I../USBX/Target -I../NetXDuo/App -I../NetXDuo/Target -I../Drivers/STM32F7xx_HAL_Driver/Inc -I../Drivers/STM32F7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F7xx/Include -I../Drivers/CMSIS/Include -I../Drivers/BSP/Components/lan8742/ -I../Middlewares/ST/netxduo/common/drivers/ethernet/ -I../Middlewares/ST/usbx/common/core/inc/ -I../Middlewares/ST/usbx/ports/generic/inc/ -I../Middlewares/ST/usbx/common/usbx_stm32_device_controllers/ -I../Middlewares/ST/netxduo/addons/dhcp/ -I../Middlewares/ST/netxduo/common/inc/ -I../Middlewares/ST/netxduo/ports/cortex_m7/gnu/inc/ -I../Middlewares/ST/threadx/common/inc/ -I../Middlewares/ST/threadx/ports/cortex_m7/gnu/inc/ -I../Middlewares/ST/guix/common/inc/ -I../Middlewares/ST/guix/ports/cortex_m7/gnu/inc/ -x assembler-with-cpp -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -o "$@" "$<"

clean: clean-Core-2f-Src

clean-Core-2f-Src:
	-$(RM) ./Core/Src/app_vnc_frame.cyclo ./Core/Src/app_vnc_frame.d ./Core/Src/app_vnc_frame.o ./Core/Src/app_vnc_frame.su
	-$(RM) ./Core/Src/app_bme280.cyclo ./Core/Src/app_bme280.d ./Core/Src/app_bme280.o ./Core/Src/app_bme280.su ./Core/Src/app_guix.cyclo ./Core/Src/app_guix.d ./Core/Src/app_guix.o ./Core/Src/app_guix.su ./Core/Src/app_state.cyclo ./Core/Src/app_state.d ./Core/Src/app_state.o ./Core/Src/app_state.su ./Core/Src/app_threadx.cyclo ./Core/Src/app_threadx.d ./Core/Src/app_threadx.o ./Core/Src/app_threadx.su ./Core/Src/bme280_sensor.cyclo ./Core/Src/bme280_sensor.d ./Core/Src/bme280_sensor.o ./Core/Src/bme280_sensor.su ./Core/Src/debug_log.cyclo ./Core/Src/debug_log.d ./Core/Src/debug_log.o ./Core/Src/debug_log.su ./Core/Src/dma2d.cyclo ./Core/Src/dma2d.d ./Core/Src/dma2d.o ./Core/Src/dma2d.su ./Core/Src/eth.cyclo ./Core/Src/eth.d ./Core/Src/eth.o ./Core/Src/eth.su ./Core/Src/fmc.cyclo ./Core/Src/fmc.d ./Core/Src/fmc.o ./Core/Src/fmc.su ./Core/Src/gpio.cyclo ./Core/Src/gpio.d ./Core/Src/gpio.o ./Core/Src/gpio.su ./Core/Src/i2c.cyclo ./Core/Src/i2c.d ./Core/Src/i2c.o ./Core/Src/i2c.su ./Core/Src/ltdc.cyclo ./Core/Src/ltdc.d ./Core/Src/ltdc.o ./Core/Src/ltdc.su ./Core/Src/main.cyclo ./Core/Src/main.d ./Core/Src/main.o ./Core/Src/main.su ./Core/Src/stm32f7xx_hal_msp.cyclo ./Core/Src/stm32f7xx_hal_msp.d ./Core/Src/stm32f7xx_hal_msp.o ./Core/Src/stm32f7xx_hal_msp.su ./Core/Src/stm32f7xx_hal_timebase_tim.cyclo ./Core/Src/stm32f7xx_hal_timebase_tim.d ./Core/Src/stm32f7xx_hal_timebase_tim.o ./Core/Src/stm32f7xx_hal_timebase_tim.su ./Core/Src/stm32f7xx_it.cyclo ./Core/Src/stm32f7xx_it.d ./Core/Src/stm32f7xx_it.o ./Core/Src/stm32f7xx_it.su ./Core/Src/syscalls.cyclo ./Core/Src/syscalls.d ./Core/Src/syscalls.o ./Core/Src/syscalls.su ./Core/Src/sysmem.cyclo ./Core/Src/sysmem.d ./Core/Src/sysmem.o ./Core/Src/sysmem.su ./Core/Src/system_stm32f7xx.cyclo ./Core/Src/system_stm32f7xx.d ./Core/Src/system_stm32f7xx.o ./Core/Src/system_stm32f7xx.su ./Core/Src/tx_initialize_low_level.d ./Core/Src/tx_initialize_low_level.o ./Core/Src/usb_otg.cyclo ./Core/Src/usb_otg.d ./Core/Src/usb_otg.o ./Core/Src/usb_otg.su

.PHONY: clean-Core-2f-Src

