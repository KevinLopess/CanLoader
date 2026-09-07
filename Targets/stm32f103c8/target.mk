BUILD_DIR := $(BUILD_ROOT)/$(TARGET)/bootloader
CPU_FLAGS := -mcpu=cortex-m3 -mthumb -mfloat-abi=soft
DEFINES := -DUSE_HAL_DRIVER -DSTM32F103xB -DBOOT_TARGET_STM32F103C8
INCLUDES := $(COMMON_INCLUDES) \
	-ITargets/stm32f103c8/Core/Inc \
	-IDrivers/STM32F1xx_HAL_Driver/Inc \
	-IDrivers/STM32F1xx_HAL_Driver/Inc/Legacy \
	-IDrivers/CMSIS/Device/ST/STM32F1xx/Include \
	-IDrivers/CMSIS/Include
LINKER_SCRIPT := Targets/stm32f103c8/STM32F103C8TX_BOOT_FLASH.ld
TARGET_C_SOURCES := \
	$(wildcard Portable/stm32f103c8/Src/*.c) \
	$(wildcard Targets/stm32f103c8/Core/Src/*.c) \
	Core/Src/syscalls.c \
	Core/Src/sysmem.c \
	$(wildcard Drivers/STM32F1xx_HAL_Driver/Src/*.c)
ASM_SOURCES := Targets/stm32f103c8/Core/Startup/startup_stm32f103c8tx.s
