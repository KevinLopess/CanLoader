ROOT := ../..
BUILD_DIR := build
CC ?= cc

CPPFLAGS += -I$(ROOT)/Config \
            -I$(ROOT)/Portable/Inc \
            -I$(ROOT)/Protocol/Inc \
            -I$(ROOT)/Application/Inc
CFLAGS ?= -O0 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror
SANITIZER_FLAGS ?=
CFLAGS += $(SANITIZER_FLAGS)
LDFLAGS += $(SANITIZER_FLAGS)

PROGRAMS := $(BUILD_DIR)/test_protocol \
            $(BUILD_DIR)/test_j1939_address_claim \
            $(BUILD_DIR)/test_j1939_tp \
            $(BUILD_DIR)/test_device_config_v2 \
            $(BUILD_DIR)/test_device_config_g0 \
            $(BUILD_DIR)/test_bootloader \
            $(BUILD_DIR)/test_bootloader_g0 \
            $(BUILD_DIR)/test_metadata_image \
            $(BUILD_DIR)/test_metadata_image_g0

.PHONY: all check clean

all: $(PROGRAMS)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/test_protocol: test_protocol.c \
                             $(ROOT)/Protocol/Src/boot_crc32.c \
                             $(ROOT)/Protocol/Src/boot_j1939.c \
                             $(ROOT)/Protocol/Src/boot_protocol.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/test_j1939_address_claim: test_j1939_address_claim.c \
                                           $(ROOT)/Protocol/Src/boot_j1939.c \
                                           $(ROOT)/Protocol/Src/boot_j1939_address_claim.c \
                                           $(ROOT)/Protocol/Src/boot_protocol.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/test_j1939_tp: test_j1939_tp.c \
                             $(ROOT)/Protocol/Src/boot_j1939.c \
                             $(ROOT)/Protocol/Src/boot_j1939_tp.c \
                             $(ROOT)/Protocol/Src/boot_protocol.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/test_device_config_v2: test_device_config_v2.c \
                                     $(ROOT)/Application/Src/boot_device_config.c \
                                     $(ROOT)/Protocol/Src/boot_crc32.c \
                                     $(ROOT)/Protocol/Src/boot_j1939.c \
                                     $(ROOT)/Protocol/Src/boot_protocol.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/test_device_config_g0: test_device_config_v2.c \
                                     $(ROOT)/Application/Src/boot_device_config.c \
                                     $(ROOT)/Protocol/Src/boot_crc32.c \
                                     $(ROOT)/Protocol/Src/boot_j1939.c \
                                     $(ROOT)/Protocol/Src/boot_protocol.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) -DBOOT_TARGET_STM32G0B1RE $(CFLAGS) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/test_bootloader: test_bootloader.c \
                               $(ROOT)/Application/Src/bootloader.c \
                               $(ROOT)/Protocol/Src/boot_j1939.c \
                               $(ROOT)/Protocol/Src/boot_j1939_address_claim.c \
                               $(ROOT)/Protocol/Src/boot_j1939_tp.c \
                               $(ROOT)/Protocol/Src/boot_protocol.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/test_bootloader_g0: test_bootloader.c \
                               $(ROOT)/Application/Src/bootloader.c \
                               $(ROOT)/Protocol/Src/boot_j1939.c \
                               $(ROOT)/Protocol/Src/boot_j1939_address_claim.c \
                               $(ROOT)/Protocol/Src/boot_j1939_tp.c \
                               $(ROOT)/Protocol/Src/boot_protocol.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) -DBOOT_TARGET_STM32G0B1RE $(CFLAGS) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/test_metadata_image: test_metadata_image.c \
                                  $(ROOT)/Application/Src/boot_metadata.c \
                                  $(ROOT)/Application/Src/boot_image.c \
                                  $(ROOT)/Protocol/Src/boot_crc32.c \
                                  $(ROOT)/Protocol/Src/boot_j1939.c \
                                  $(ROOT)/Protocol/Src/boot_protocol.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/test_metadata_image_g0: test_metadata_image.c \
                                  $(ROOT)/Application/Src/boot_metadata.c \
                                  $(ROOT)/Application/Src/boot_image.c \
                                  $(ROOT)/Protocol/Src/boot_crc32.c \
                                  $(ROOT)/Protocol/Src/boot_j1939.c \
                                  $(ROOT)/Protocol/Src/boot_protocol.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) -DBOOT_TARGET_STM32G0B1RE $(CFLAGS) $^ $(LDFLAGS) -o $@

check: all
	$(BUILD_DIR)/test_protocol
	$(BUILD_DIR)/test_j1939_address_claim
	$(BUILD_DIR)/test_j1939_tp
	$(BUILD_DIR)/test_device_config_v2
	$(BUILD_DIR)/test_device_config_g0
	$(BUILD_DIR)/test_bootloader
	$(BUILD_DIR)/test_bootloader_g0
	$(BUILD_DIR)/test_metadata_image
	$(BUILD_DIR)/test_metadata_image_g0

clean:
	$(RM) $(PROGRAMS)
