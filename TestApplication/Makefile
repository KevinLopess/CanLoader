APP_NAME := canloader_test_app
PROJECT_ROOT := ..
TARGET ?= stm32f103c8
APP_VARIANT ?= 1
BUILD_DIR ?= $(PROJECT_ROOT)/build/$(TARGET)/test-application$(if $(filter-out 1,$(APP_VARIANT)),-$(APP_VARIANT))

STM32_GNU_BIN ?= $(firstword \
	$(wildcard $(HOME)/apps/stm32cubeide/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.*/tools/bin) \
	$(wildcard /Applications/STM32CubeIDE.app/Contents/Eclipse/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.*/tools/bin))
TOOLCHAIN ?= $(if $(STM32_GNU_BIN),$(STM32_GNU_BIN)/arm-none-eabi-,arm-none-eabi-)
CC := $(TOOLCHAIN)gcc
OBJCOPY := $(TOOLCHAIN)objcopy
SIZE := $(TOOLCHAIN)size

COMMON_INCLUDES := -ICommon \
	-I$(PROJECT_ROOT)/Application/Inc \
	-I$(PROJECT_ROOT)/Config \
	-I$(PROJECT_ROOT)/Portable/Inc \
	-I$(PROJECT_ROOT)/Protocol/Inc \
	-I$(PROJECT_ROOT)/Drivers/CMSIS/Include

COMMON_C_SOURCES := Common/app_activity.c \
	$(PROJECT_ROOT)/Application/Src/boot_device_config.c \
	$(PROJECT_ROOT)/Application/Src/boot_metadata.c \
	$(PROJECT_ROOT)/Protocol/Src/boot_crc32.c \
	$(PROJECT_ROOT)/Protocol/Src/boot_j1939.c \
	$(PROJECT_ROOT)/Protocol/Src/boot_j1939_address_claim.c \
	$(PROJECT_ROOT)/Protocol/Src/boot_j1939_tp.c \
	$(PROJECT_ROOT)/Protocol/Src/boot_protocol.c

TARGET_CONFIG := Targets/$(TARGET)/target.mk
ifeq ($(wildcard $(TARGET_CONFIG)),)
$(error Unsupported TARGET '$(TARGET)'; missing TestApplication/$(TARGET_CONFIG))
endif
include $(TARGET_CONFIG)
DEFINES += -DAPP_VARIANT=$(APP_VARIANT)U

CFLAGS := $(CPU_FLAGS) -std=c11 -Os -g3 \
	-ffreestanding -fno-builtin -ffunction-sections -fdata-sections \
	-Wall -Wextra -Werror -MMD -MP
ASFLAGS := $(CPU_FLAGS) -x assembler-with-cpp -MMD -MP
LDFLAGS := $(CPU_FLAGS) -nostdlib -T$(LINKER_SCRIPT) \
	-Wl,--gc-sections -Wl,--cref -Wl,--print-memory-usage \
	-Wl,-Map=$(BUILD_DIR)/$(APP_NAME).map
LDLIBS := -lgcc

C_SOURCES := $(TARGET_C_SOURCES_PREFIX) $(COMMON_C_SOURCES) \
	$(TARGET_C_SOURCES_SUFFIX)
VPATH := $(sort $(dir $(C_SOURCES) $(ASM_SOURCES)))
OBJECTS := $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o))) \
	$(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
DEPS := $(OBJECTS:.o=.d)

ELF := $(BUILD_DIR)/$(APP_NAME).elf
BIN := $(BUILD_DIR)/$(APP_NAME).bin
HEX := $(BUILD_DIR)/$(APP_NAME).hex

.PHONY: all clean

all: $(BIN) $(HEX)
	$(SIZE) $(ELF)

$(ELF): $(OBJECTS) $(LINKER_SCRIPT) | $(BUILD_DIR)
	$(CC) $(LDFLAGS) $(OBJECTS) $(LDLIBS) -o $@

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

$(HEX): $(ELF)
	$(OBJCOPY) -O ihex $< $@

$(BUILD_DIR)/%.o: %.c Makefile $(TARGET_CONFIG) | $(BUILD_DIR)
	$(CC) $(DEFINES) $(INCLUDES) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.s Makefile $(TARGET_CONFIG) | $(BUILD_DIR)
	$(CC) $(DEFINES) $(INCLUDES) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $@

clean:
	$(RM) -r -- $(BUILD_DIR)

-include $(DEPS)
