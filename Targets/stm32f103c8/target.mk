BUILD_DIR := build/bootloader
CPU_FLAGS := -mcpu=cortex-m3 -mthumb -mfloat-abi=soft
DEFINES := -DUSE_HAL_DRIVER -DSTM32F103xB -DBOOT_TARGET_STM32F103C8
INCLUDES := $(COMMON_INCLUDES) \
	-IPortable/stm32f1/Inc \
	-ICore/Inc \
	-IDrivers/STM32F1xx_HAL_Driver/Inc \
	-IDrivers/STM32F1xx_HAL_Driver/Inc/Legacy \
	-IDrivers/CMSIS/Device/ST/STM32F1xx/Include \
	-IDrivers/CMSIS/Include
LINKER_SCRIPT := STM32F103C8TX_FLASH.ld
TARGET_C_SOURCES := \
	$(wildcard Portable/stm32f1/Src/*.c) \
	$(wildcard Core/Src/*.c) \
	$(wildcard Drivers/STM32F1xx_HAL_Driver/Src/*.c)
ASM_SOURCES := Core/Startup/startup_stm32f103c8tx.s
TEST_APPLICATION_ARGS := PLATFORM=stm32f103c8 TARGET=canloader_test_app
