BUILD_DIR := $(BUILD_ROOT)/$(TARGET)/bootloader
CPU_FLAGS := -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft
DEFINES := -DUSE_HAL_DRIVER -DSTM32G0B1xx -DBOOT_TARGET_STM32G0B1RE
INCLUDES := $(COMMON_INCLUDES) \
	-ITargets/stm32g0b1re/Core/Inc \
	-IDrivers/STM32G0xx_HAL_Driver/Inc \
	-IDrivers/STM32G0xx_HAL_Driver/Inc/Legacy \
	-IDrivers/CMSIS/Device/ST/STM32G0xx/Include \
	-IDrivers/CMSIS/Include
LINKER_SCRIPT := Targets/stm32g0b1re/STM32G0B1RETX_BOOT_FLASH.ld
TARGET_C_SOURCES := \
	$(wildcard Portable/stm32g0b1re/Src/*.c) \
	$(wildcard Targets/stm32g0b1re/Core/Src/*.c) \
	Core/Src/syscalls.c \
	Core/Src/sysmem.c \
	Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal.c \
	Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_cortex.c \
	Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_fdcan.c \
	Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_flash.c \
	Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_flash_ex.c \
	Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_gpio.c \
	Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_pwr.c \
	Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_pwr_ex.c \
	Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_rcc.c \
	Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_rcc_ex.c
ASM_SOURCES := Targets/stm32g0b1re/Core/Startup/startup_stm32g0b1xx.s
