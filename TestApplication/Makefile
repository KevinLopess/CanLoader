TARGET := canloader_test_app
BUILD_DIR := build
PROJECT_ROOT := ..

STM32_GNU_BIN ?= $(firstword \
	$(wildcard $(HOME)/apps/stm32cubeide/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.*/tools/bin) \
	$(wildcard /Applications/STM32CubeIDE.app/Contents/Eclipse/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.*/tools/bin))
TOOLCHAIN ?= $(if $(STM32_GNU_BIN),$(STM32_GNU_BIN)/arm-none-eabi-,arm-none-eabi-)
CC := $(TOOLCHAIN)gcc
OBJCOPY := $(TOOLCHAIN)objcopy
SIZE := $(TOOLCHAIN)size

CPU_FLAGS := -mcpu=cortex-m3 -mthumb
DEFINES := -DSTM32F103xB
INCLUDES := \
	-I$(PROJECT_ROOT)/Application/Inc \
	-I$(PROJECT_ROOT)/Config \
	-I$(PROJECT_ROOT)/Portable/Inc \
	-I$(PROJECT_ROOT)/Protocol/Inc \
	-I$(PROJECT_ROOT)/Drivers/CMSIS/Include \
	-I$(PROJECT_ROOT)/Drivers/CMSIS/Device/ST/STM32F1xx/Include

CFLAGS := $(CPU_FLAGS) -std=c11 -Os -g3 \
	-ffreestanding -fno-builtin -ffunction-sections -fdata-sections \
	-Wall -Wextra -Werror -MMD -MP
ASFLAGS := $(CPU_FLAGS) -x assembler-with-cpp -MMD -MP
LDFLAGS := $(CPU_FLAGS) -nostdlib -Tlinker.ld \
	-Wl,--gc-sections -Wl,--cref -Wl,--print-memory-usage \
	-Wl,-Map=$(BUILD_DIR)/$(TARGET).map
LDLIBS := -lgcc

C_SOURCES := \
	Src/main.c \
	Src/system_stm32f1xx.c \
	$(PROJECT_ROOT)/Application/Src/boot_device_config.c \
	$(PROJECT_ROOT)/Application/Src/boot_metadata.c \
	$(PROJECT_ROOT)/Protocol/Src/boot_crc32.c \
	$(PROJECT_ROOT)/Protocol/Src/boot_j1939.c \
	$(PROJECT_ROOT)/Protocol/Src/boot_j1939_address_claim.c \
	$(PROJECT_ROOT)/Protocol/Src/boot_j1939_tp.c \
	$(PROJECT_ROOT)/Protocol/Src/boot_protocol.c
ASM_SOURCE := $(PROJECT_ROOT)/Core/Startup/startup_stm32f103c8tx.s

OBJECTS := \
	$(BUILD_DIR)/main.o \
	$(BUILD_DIR)/system_stm32f1xx.o \
	$(BUILD_DIR)/boot_device_config.o \
	$(BUILD_DIR)/boot_metadata.o \
	$(BUILD_DIR)/boot_crc32.o \
	$(BUILD_DIR)/boot_j1939.o \
	$(BUILD_DIR)/boot_j1939_address_claim.o \
	$(BUILD_DIR)/boot_j1939_tp.o \
	$(BUILD_DIR)/boot_protocol.o \
	$(BUILD_DIR)/startup_stm32f103c8tx.o
DEPS := $(OBJECTS:.o=.d)

ELF := $(BUILD_DIR)/$(TARGET).elf
BIN := $(BUILD_DIR)/$(TARGET).bin
HEX := $(BUILD_DIR)/$(TARGET).hex

.PHONY: all clean

all: $(BIN) $(HEX)
	$(SIZE) $(ELF)

$(ELF): $(OBJECTS) linker.ld | $(BUILD_DIR)
	$(CC) $(LDFLAGS) $(OBJECTS) $(LDLIBS) -o $@

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

$(HEX): $(ELF)
	$(OBJCOPY) -O ihex $< $@

$(BUILD_DIR)/main.o: Src/main.c Makefile | $(BUILD_DIR)
	$(CC) $(DEFINES) $(INCLUDES) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/system_stm32f1xx.o: Src/system_stm32f1xx.c Makefile | $(BUILD_DIR)
	$(CC) $(DEFINES) $(INCLUDES) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/boot_crc32.o: $(PROJECT_ROOT)/Protocol/Src/boot_crc32.c Makefile | $(BUILD_DIR)
	$(CC) $(DEFINES) $(INCLUDES) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/boot_device_config.o: $(PROJECT_ROOT)/Application/Src/boot_device_config.c Makefile | $(BUILD_DIR)
	$(CC) $(DEFINES) $(INCLUDES) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/boot_metadata.o: $(PROJECT_ROOT)/Application/Src/boot_metadata.c Makefile | $(BUILD_DIR)
	$(CC) $(DEFINES) $(INCLUDES) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/boot_j1939.o: $(PROJECT_ROOT)/Protocol/Src/boot_j1939.c Makefile | $(BUILD_DIR)
	$(CC) $(DEFINES) $(INCLUDES) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/boot_j1939_address_claim.o: $(PROJECT_ROOT)/Protocol/Src/boot_j1939_address_claim.c Makefile | $(BUILD_DIR)
	$(CC) $(DEFINES) $(INCLUDES) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/boot_j1939_tp.o: $(PROJECT_ROOT)/Protocol/Src/boot_j1939_tp.c Makefile | $(BUILD_DIR)
	$(CC) $(DEFINES) $(INCLUDES) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/boot_protocol.o: $(PROJECT_ROOT)/Protocol/Src/boot_protocol.c Makefile | $(BUILD_DIR)
	$(CC) $(DEFINES) $(INCLUDES) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/startup_stm32f103c8tx.o: $(ASM_SOURCE) Makefile | $(BUILD_DIR)
	$(CC) $(DEFINES) $(INCLUDES) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $@

clean:
	$(RM) -r -- $(BUILD_DIR)

-include $(DEPS)
