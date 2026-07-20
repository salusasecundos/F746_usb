################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

-include ../makefile.init

RM := rm -rf

# All of the sources participating in the build are defined here
-include sources.mk
-include USBX/App/subdir.mk
-include NetXDuo/App/subdir.mk
-include Middlewares/ST/usbx/common/usbx_stm32_device_controllers/subdir.mk
-include Middlewares/ST/usbx/common/core/src/subdir.mk
-include Middlewares/ST/threadx/ports/cortex_m7/gnu/src/subdir.mk
-include Middlewares/ST/threadx/common/src/subdir.mk
-include Middlewares/ST/netxduo/common/src/subdir.mk
-include Middlewares/ST/netxduo/common/drivers/ethernet/lan8742/subdir.mk
-include Middlewares/ST/netxduo/common/drivers/ethernet/subdir.mk
-include Middlewares/ST/netxduo/addons/dhcp/subdir.mk
-include Middlewares/ST/guix/common/src/subdir.mk
-include Drivers/STM32F7xx_HAL_Driver/Src/subdir.mk
-include Drivers/BSP/Components/lan8742/subdir.mk
-include Core/Startup/subdir.mk
-include Core/Src/subdir.mk
-include AZURE_RTOS/App/subdir.mk
-include objects.mk

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(S_DEPS)),)
-include $(S_DEPS)
endif
ifneq ($(strip $(S_UPPER_DEPS)),)
-include $(S_UPPER_DEPS)
endif
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
endif

-include ../makefile.defs

OPTIONAL_TOOL_DEPS := \
$(wildcard ../makefile.defs) \
$(wildcard ../makefile.init) \
$(wildcard ../makefile.targets) \


BUILD_ARTIFACT_NAME := F746_USB
BUILD_ARTIFACT_EXTENSION := elf
BUILD_ARTIFACT_PREFIX :=
BUILD_ARTIFACT := $(BUILD_ARTIFACT_PREFIX)$(BUILD_ARTIFACT_NAME)$(if $(BUILD_ARTIFACT_EXTENSION),.$(BUILD_ARTIFACT_EXTENSION),)

# Add inputs and outputs from these tool invocations to the build variables 
EXECUTABLES += \
F746_USB.elf \

MAP_FILES += \
F746_USB.map \

SIZE_OUTPUT += \
default.size.stdout \

OBJDUMP_LIST += \
F746_USB.list \

OBJCOPY_HEX += \
F746_USB.hex \


# All Target
all: main-build

# Main-build Target
main-build: F746_USB.elf secondary-outputs

# Tool invocations
F746_USB.elf F746_USB.map: $(OBJS) $(USER_OBJS) ../STM32F746NGHX_FLASH.ld makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-gcc -o "F746_USB.elf" @"objects.list" $(USER_OBJS) $(LIBS) -mcpu=cortex-m7 -T"../STM32F746NGHX_FLASH.ld" --specs=nosys.specs -Wl,-Map="F746_USB.map" -Wl,--gc-sections -static --specs=nano.specs -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -Wl,--start-group -lc -lm -Wl,--end-group
	@echo 'Finished building target: $@'
	@echo ' '

default.size.stdout: $(EXECUTABLES) makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-size  $(EXECUTABLES)
	@echo 'Finished building: $@'
	@echo ' '

F746_USB.list: $(EXECUTABLES) makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-objdump -h -S $(EXECUTABLES) > "F746_USB.list"
	@echo 'Finished building: $@'
	@echo ' '

F746_USB.hex: $(EXECUTABLES) makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-objcopy -O ihex $(EXECUTABLES) "F746_USB.hex"
	@echo 'Finished building: $@'
	@echo ' '

# Other Targets
clean:
	-$(RM) F746_USB.elf F746_USB.hex F746_USB.list F746_USB.map default.size.stdout
	-@echo ' '

secondary-outputs: $(SIZE_OUTPUT) $(OBJDUMP_LIST) $(OBJCOPY_HEX)

fail-specified-linker-script-missing:
	@echo 'Error: Cannot find the specified linker script. Check the linker settings in the build configuration.'
	@exit 2

warn-no-linker-script-specified:
	@echo 'Warning: No linker script specified. Check the linker settings in the build configuration.'

.PHONY: all clean dependents main-build fail-specified-linker-script-missing warn-no-linker-script-specified

-include ../makefile.targets
