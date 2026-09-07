CPU_FLAGS := -mcpu=cortex-m3 -mthumb
DEFINES := -DSTM32F103xB
INCLUDES := $(COMMON_INCLUDES) \
	-I$(PROJECT_ROOT)/Drivers/CMSIS/Device/ST/STM32F1xx/Include
LINKER_SCRIPT := Targets/stm32f103c8/linker.ld
TARGET_C_SOURCES_PREFIX := \
	Targets/stm32f103c8/Src/main_stm32f103c8.c \
	Targets/stm32f103c8/Src/system_stm32f1xx.c
ASM_SOURCES := $(PROJECT_ROOT)/Targets/stm32f103c8/Core/Startup/startup_stm32f103c8tx.s
