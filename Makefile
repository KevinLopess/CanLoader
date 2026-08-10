PROJECT := CanLoader
BUILD_DIR := build/bootloader
STM32_GNU_BIN ?= $(firstword $(wildcard $(HOME)/apps/stm32cubeide/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.*/tools/bin))
TOOLCHAIN_PREFIX ?= $(if $(STM32_GNU_BIN),$(STM32_GNU_BIN)/arm-none-eabi-,arm-none-eabi-)
CC := $(TOOLCHAIN_PREFIX)gcc
OBJCOPY := $(TOOLCHAIN_PREFIX)objcopy
OBJDUMP := $(TOOLCHAIN_PREFIX)objdump
SIZE := $(TOOLCHAIN_PREFIX)size

CPU_FLAGS := -mcpu=cortex-m3 -mthumb -mfloat-abi=soft
DEFINES := -DUSE_HAL_DRIVER -DSTM32F103xB
INCLUDES := \
	-IApplication/Inc \
	-IProtocol/Inc \
	-IPortable/Inc \
	-IPortable/stm32f1/Inc \
	-IConfig \
	-ICore/Inc \
	-IDrivers/STM32F1xx_HAL_Driver/Inc \
	-IDrivers/STM32F1xx_HAL_Driver/Inc/Legacy \
	-IDrivers/CMSIS/Device/ST/STM32F1xx/Include \
	-IDrivers/CMSIS/Include

CFLAGS := $(CPU_FLAGS) -std=gnu11 -Os -g3 $(DEFINES) $(INCLUDES) \
	-ffunction-sections -fdata-sections -Wall -Wextra -MMD -MP
ASFLAGS := $(CPU_FLAGS) -x assembler-with-cpp -g3 -MMD -MP
LDFLAGS := $(CPU_FLAGS) -TSTM32F103C8TX_FLASH.ld --specs=nosys.specs \
	--specs=nano.specs -Wl,-Map=$(BUILD_DIR)/$(PROJECT).map \
	-Wl,--gc-sections -static -Wl,--start-group -lc -lm -Wl,--end-group

C_SOURCES := \
	$(wildcard Application/Src/*.c) \
	$(wildcard Protocol/Src/*.c) \
	$(wildcard Portable/stm32f1/Src/*.c) \
	$(wildcard Core/Src/*.c) \
	$(wildcard Drivers/STM32F1xx_HAL_Driver/Src/*.c)
ASM_SOURCES := Core/Startup/startup_stm32f103c8tx.s
C_OBJECTS := $(addprefix $(BUILD_DIR)/,$(C_SOURCES:.c=.o))
ASM_OBJECTS := $(addprefix $(BUILD_DIR)/,$(ASM_SOURCES:.s=.o))
OBJECTS := $(C_OBJECTS) $(ASM_OBJECTS)
DEPS := $(OBJECTS:.o=.d)

.PHONY: all bootloader test-application clean

all: bootloader test-application

bootloader: $(BUILD_DIR)/$(PROJECT).elf $(BUILD_DIR)/$(PROJECT).bin $(BUILD_DIR)/$(PROJECT).hex

$(BUILD_DIR)/$(PROJECT).elf: $(OBJECTS) STM32F103C8TX_FLASH.ld
	@mkdir -p $(@D)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	$(SIZE) $@

$(BUILD_DIR)/$(PROJECT).bin: $(BUILD_DIR)/$(PROJECT).elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/$(PROJECT).hex: $(BUILD_DIR)/$(PROJECT).elf
	$(OBJCOPY) -O ihex $< $@
	$(OBJDUMP) -h -S $< > $(BUILD_DIR)/$(PROJECT).list

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.s
	@mkdir -p $(@D)
	$(CC) $(ASFLAGS) -c $< -o $@

test-application:
	$(MAKE) -C TestApplication TOOLCHAIN=$(TOOLCHAIN_PREFIX) all

clean:
	$(RM) -r build
	$(MAKE) -C TestApplication clean

-include $(DEPS)
