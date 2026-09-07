CPU_FLAGS := -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft
DEFINES := \
	-DSTM32G0B1xx \
	-DUSE_HAL_DRIVER \
	-DBOOT_TARGET_STM32G0B1RE \
	-DUSER_VECT_TAB_ADDRESS \
	-DVECT_TAB_OFFSET=0x00008000U
INCLUDES := $(COMMON_INCLUDES) \
	-I$(PROJECT_ROOT)/Targets/stm32g0b1re/Core/Inc \
	-I$(PROJECT_ROOT)/Drivers/STM32G0xx_HAL_Driver/Inc \
	-I$(PROJECT_ROOT)/Drivers/STM32G0xx_HAL_Driver/Inc/Legacy \
	-I$(PROJECT_ROOT)/Drivers/CMSIS/Device/ST/STM32G0xx/Include
LINKER_SCRIPT := Targets/stm32g0b1re/linker.ld
TARGET_C_SOURCES_PREFIX := \
	Targets/stm32g0b1re/Src/main_stm32g0b1re.c \
	$(PROJECT_ROOT)/Targets/stm32g0b1re/Core/Src/system_stm32g0xx.c \
	$(PROJECT_ROOT)/Targets/stm32g0b1re/Core/Src/stm32g0xx_hal_msp.c
TARGET_C_SOURCES_SUFFIX := \
	$(PROJECT_ROOT)/Portable/stm32g0b1re/Src/boot_can_stm32g0.c \
	$(PROJECT_ROOT)/Portable/stm32g0b1re/Src/boot_flash_stm32g0.c \
	$(PROJECT_ROOT)/Portable/stm32g0b1re/Src/boot_platform_stm32g0.c \
	$(PROJECT_ROOT)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal.c \
	$(PROJECT_ROOT)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_cortex.c \
	$(PROJECT_ROOT)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_fdcan.c \
	$(PROJECT_ROOT)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_flash.c \
	$(PROJECT_ROOT)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_flash_ex.c \
	$(PROJECT_ROOT)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_gpio.c \
	$(PROJECT_ROOT)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_pwr.c \
	$(PROJECT_ROOT)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_pwr_ex.c \
	$(PROJECT_ROOT)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_rcc.c \
	$(PROJECT_ROOT)/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_rcc_ex.c
ASM_SOURCES := $(PROJECT_ROOT)/Targets/stm32g0b1re/Core/Startup/startup_stm32g0b1xx.s
