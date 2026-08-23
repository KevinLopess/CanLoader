#ifndef BOOT_CONFIG_H
#define BOOT_CONFIG_H

#include <stdint.h>

/* Target memory map: STM32F103C8, 64 KiB Flash and 20 KiB SRAM. */
#define BOOT_FLASH_BASE_ADDRESS       UINT32_C(0x08000000)
#define BOOT_FLASH_TOTAL_SIZE         UINT32_C(0x00010000)
#define BOOTLOADER_FLASH_SIZE         UINT32_C(0x00004000)
#define BOOTLOADER_CODE_SIZE          UINT32_C(0x00003C00)
#define BOOT_DEVICE_CONFIG_PAGE_SIZE  UINT32_C(0x00000400)
#define BOOT_DEVICE_CONFIG_ADDRESS    (BOOT_FLASH_BASE_ADDRESS + BOOTLOADER_CODE_SIZE)
#define BOOT_APPLICATION_ADDRESS      (BOOT_FLASH_BASE_ADDRESS + BOOTLOADER_FLASH_SIZE)
#define BOOT_METADATA_PAGE_SIZE       UINT32_C(0x00000400)
#define BOOT_METADATA_ADDRESS         (BOOT_FLASH_BASE_ADDRESS + BOOT_FLASH_TOTAL_SIZE - BOOT_METADATA_PAGE_SIZE)
#define BOOT_APPLICATION_MAX_SIZE     (BOOT_METADATA_ADDRESS - BOOT_APPLICATION_ADDRESS)

#define BOOT_SRAM_START_ADDRESS       UINT32_C(0x20000000)
#define BOOT_SRAM_SIZE                UINT32_C(0x00005000)
#define BOOT_SRAM_END_ADDRESS         (BOOT_SRAM_START_ADDRESS + BOOT_SRAM_SIZE)

#define BOOT_FLASH_PAGE_SIZE          UINT32_C(1024)
#define BOOT_FLASH_PROGRAM_UNIT       UINT32_C(2)

/* Standard 11-bit CAN identifiers. A node occupies one request/response set. */
#define BOOT_DEFAULT_NODE_ID          UINT8_C(1)
#define BOOT_MIN_NODE_ID              UINT8_C(1)
#define BOOT_MAX_NODE_ID              UINT8_C(0x7F)
#define BOOT_CAN_RESPONSE_BASE_ID     UINT32_C(0x580)
#define BOOT_CAN_COMMAND_BASE_ID      UINT32_C(0x600)
#define BOOT_CAN_DATA_BASE_ID         UINT32_C(0x700)
#define BOOT_CAN_BROADCAST_ID         UINT32_C(0x500)
#define BOOT_CAN_RESPONSE_ID(node)    (BOOT_CAN_RESPONSE_BASE_ID + (uint32_t)(node))
#define BOOT_CAN_COMMAND_ID(node)     (BOOT_CAN_COMMAND_BASE_ID + (uint32_t)(node))
#define BOOT_CAN_DATA_ID(node)        (BOOT_CAN_DATA_BASE_ID + (uint32_t)(node))

#define BOOT_PROTOCOL_VERSION         UINT8_C(1)
#define BOOT_TARGET_CODE              UINT8_C(0xF1)

#define BOOT_TRANSFER_BLOCK_SIZE      UINT32_C(100)
#define BOOT_DATA_BYTES_PER_FRAME     UINT32_C(5)
#define BOOT_RX_BUDGET_PER_TASK       UINT32_C(8)

#define BOOT_WINDOW_MS                UINT32_C(1500)
#define BOOT_REQUEST_WINDOW_MS        UINT32_C(10000)
#define BOOT_TRANSFER_TIMEOUT_MS      UINT32_C(5000)
#define BOOT_RUN_RESPONSE_DELAY_MS    UINT32_C(50)
#define BOOT_RUN_RESPONSE_TIMEOUT_MS  UINT32_C(250)
#define BOOT_LED_FAST_MS              UINT32_C(125)
#define BOOT_LED_WAIT_MS              UINT32_C(500)
#define BOOT_LED_ERROR_MS             UINT32_C(100)
#define BOOT_LED_ACTIVE_LOW           1U

#define BOOT_METADATA_MAGIC           UINT32_C(0x444D4C43) /* "CLMD" in little endian. */
#define BOOT_METADATA_FORMAT_VERSION  UINT16_C(1)

#if (BOOT_DEFAULT_NODE_ID < BOOT_MIN_NODE_ID) || \
    (BOOT_DEFAULT_NODE_ID > BOOT_MAX_NODE_ID)
#error "BOOT_DEFAULT_NODE_ID must be in the range 1..127"
#endif

#if (BOOT_CAN_DATA_BASE_ID + BOOT_MAX_NODE_ID > 0x7FFU) || \
    (BOOT_CAN_COMMAND_BASE_ID + BOOT_MAX_NODE_ID > 0x7FFU) || \
    (BOOT_CAN_RESPONSE_BASE_ID + BOOT_MAX_NODE_ID > 0x7FFU)
#error "CanLoader identifiers must fit an 11-bit CAN identifier"
#endif

#if (BOOTLOADER_CODE_SIZE + BOOT_DEVICE_CONFIG_PAGE_SIZE != BOOTLOADER_FLASH_SIZE)
#error "Bootloader code and device configuration must fill the bootloader partition"
#endif

#if ((BOOT_DEVICE_CONFIG_ADDRESS % BOOT_FLASH_PAGE_SIZE) != 0U)
#error "Device configuration must be Flash-page aligned"
#endif

#if ((BOOT_APPLICATION_ADDRESS % BOOT_FLASH_PAGE_SIZE) != 0U) || \
    ((BOOT_METADATA_ADDRESS % BOOT_FLASH_PAGE_SIZE) != 0U)
#error "Application and metadata regions must be Flash-page aligned"
#endif

#if (BOOT_FLASH_PROGRAM_UNIT == 0U) || \
    ((BOOT_TRANSFER_BLOCK_SIZE % BOOT_FLASH_PROGRAM_UNIT) != 0U)
#error "Transfer blocks must be aligned to the target Flash program unit"
#endif

#endif /* BOOT_CONFIG_H */
