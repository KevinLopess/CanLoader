PROJECT := CanLoader
TARGET_ORIGIN := $(origin TARGET)
TARGET ?= stm32f103c8
BUILD_ROOT ?= build
APP_VARIANT ?= 1
TEST_APP_DIR := test-application$(if $(filter-out 1,$(APP_VARIANT)),-$(APP_VARIANT))
SUPPORTED_TARGETS := $(patsubst Targets/%/target.mk,%,$(wildcard Targets/*/target.mk))
TARGET_BUILD_GOALS := $(addprefix build-,$(SUPPORTED_TARGETS))
STM32_GNU_BIN ?= $(firstword \
	$(wildcard $(HOME)/apps/stm32cubeide/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.*/tools/bin) \
	$(wildcard /Applications/STM32CubeIDE.app/Contents/Eclipse/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.*/tools/bin))
TOOLCHAIN_PREFIX ?= $(if $(STM32_GNU_BIN),$(STM32_GNU_BIN)/arm-none-eabi-,arm-none-eabi-)
CC := $(TOOLCHAIN_PREFIX)gcc
OBJCOPY := $(TOOLCHAIN_PREFIX)objcopy
OBJDUMP := $(TOOLCHAIN_PREFIX)objdump
SIZE := $(TOOLCHAIN_PREFIX)size
PYTHON ?= python3

COMMON_INCLUDES := \
	-IApplication/Inc \
	-IProtocol/Inc \
	-IPortable/Inc \
	-IConfig

COMMON_C_SOURCES := \
	$(wildcard Application/Src/*.c) \
	$(wildcard Protocol/Src/*.c)

TARGET_CONFIG := Targets/$(TARGET)/target.mk
ifeq ($(wildcard $(TARGET_CONFIG)),)
$(error Unsupported TARGET '$(TARGET)'; missing $(TARGET_CONFIG))
endif
include $(TARGET_CONFIG)

CFLAGS := $(CPU_FLAGS) -std=gnu11 -Os -g3 $(DEFINES) $(INCLUDES) \
	-ffunction-sections -fdata-sections -Wall -Wextra -MMD -MP
ASFLAGS := $(CPU_FLAGS) -x assembler-with-cpp -g3 -MMD -MP
LDFLAGS := $(CPU_FLAGS) -T$(LINKER_SCRIPT) --specs=nosys.specs \
	--specs=nano.specs -Wl,-Map=$(BUILD_DIR)/$(PROJECT).map \
	-Wl,--gc-sections -static -Wl,--start-group -lc -lm -Wl,--end-group

C_SOURCES := $(COMMON_C_SOURCES) $(TARGET_C_SOURCES)
C_OBJECTS := $(addprefix $(BUILD_DIR)/,$(C_SOURCES:.c=.o))
ASM_OBJECTS := $(addprefix $(BUILD_DIR)/,$(ASM_SOURCES:.s=.o))
OBJECTS := $(C_OBJECTS) $(ASM_OBJECTS)
DEPS := $(OBJECTS:.o=.d)

.PHONY: all all-targets target-all $(TARGET_BUILD_GOALS) bootloader \
	test-application check-portability test-c test-python test clean

ifeq ($(TARGET_ORIGIN),undefined)
all: all-targets
else
all: target-all
endif

all-targets: $(TARGET_BUILD_GOALS)

$(TARGET_BUILD_GOALS):
	$(MAKE) TARGET=$(patsubst build-%,%,$@) target-all

target-all: bootloader test-application

bootloader: $(BUILD_DIR)/$(PROJECT).elf $(BUILD_DIR)/$(PROJECT).bin $(BUILD_DIR)/$(PROJECT).hex

$(BUILD_DIR)/$(PROJECT).elf: $(OBJECTS) $(LINKER_SCRIPT)
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
	$(MAKE) -C TestApplication TARGET=$(TARGET) APP_VARIANT=$(APP_VARIANT) \
		BUILD_DIR=$(abspath $(BUILD_ROOT)/$(TARGET)/$(TEST_APP_DIR)) \
		TOOLCHAIN=$(TOOLCHAIN_PREFIX) all

check-portability:
	sh Tests/check_portability.sh

test-c:
	$(MAKE) -C Tests/c check

test-python:
	PYTHONPATH=Tools/canloader $(PYTHON) -m pytest -q Tests/python

test: check-portability test-c test-python

clean:
	$(RM) -r -- $(BUILD_ROOT)

-include $(DEPS)
