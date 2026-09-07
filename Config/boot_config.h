#ifndef BOOT_CONFIG_H
#define BOOT_CONFIG_H

#include <stdint.h>

#if defined(BOOT_TARGET_STM32G0B1RE)
#include "targets/boot_target_stm32g0b1re.h"
#elif defined(BOOT_TARGET_STM32F103C8) || !defined(BOOT_TARGET_STM32G0B1RE)
/* Keep STM32F103C8 as the default for host-side tests and existing builds. */
#include "targets/boot_target_stm32f103c8.h"
#else
#error "Unsupported CanLoader target"
#endif

#define BOOT_DEVICE_CONFIG_ADDRESS    (BOOT_FLASH_BASE_ADDRESS + BOOTLOADER_CODE_SIZE)
#define BOOT_METADATA_ADDRESS         (BOOT_FLASH_BASE_ADDRESS + BOOT_FLASH_TOTAL_SIZE - BOOT_METADATA_PAGE_SIZE)
#define BOOT_APPLICATION_MAX_SIZE     (BOOT_METADATA_ADDRESS - BOOT_APPLICATION_ADDRESS)

#define BOOT_SRAM_END_ADDRESS         (BOOT_SRAM_START_ADDRESS + BOOT_SRAM_SIZE)

/* SAE J1939 profile. All application frames use 29-bit CAN identifiers. */
#define BOOT_PROTOCOL_VERSION                    UINT8_C(2)

#define BOOT_J1939_PRIORITY                       UINT8_C(6)
#define BOOT_J1939_GLOBAL_ADDRESS                 UINT8_C(0xFF)
#define BOOT_J1939_NULL_ADDRESS                   UINT8_C(0xFE)
#define BOOT_J1939_DEFAULT_PREFERRED_ADDRESS      UINT8_C(0x80)
#define BOOT_J1939_ARBITRARY_ADDRESS_MIN          UINT8_C(0x80)
#define BOOT_J1939_ARBITRARY_ADDRESS_MAX          UINT8_C(0xF7)

/*
 * Development NAME fields. A production device must use an SAE-assigned
 * manufacturer code and a manufacturer-controlled unique identity number.
 * The development identity number is derived deterministically from the
 * STM32 96-bit UID and truncated to the 21 bits available in NAME.
 */
#define BOOT_J1939_NAME_MANUFACTURER_CODE         UINT16_C(0)
#define BOOT_J1939_NAME_ECU_INSTANCE              UINT8_C(0)
#define BOOT_J1939_NAME_FUNCTION_INSTANCE         UINT8_C(0)
#define BOOT_J1939_NAME_FUNCTION                  UINT8_C(0)
#define BOOT_J1939_NAME_VEHICLE_SYSTEM            UINT8_C(0)
#define BOOT_J1939_NAME_VEHICLE_SYSTEM_INSTANCE   UINT8_C(0)
#define BOOT_J1939_NAME_INDUSTRY_GROUP            UINT8_C(0)
#define BOOT_J1939_NAME_ARBITRARY_ADDRESS_CAPABLE 1U

#define BOOT_RX_BUDGET_PER_TASK       UINT32_C(8)
#define BOOT_TX_QUEUE_CAPACITY        UINT32_C(8)
#define BOOT_J1939_TP_CTS_WINDOW      UINT8_C(8)

#define BOOT_WINDOW_MS                UINT32_C(1500)
#define BOOT_REQUEST_WINDOW_MS        UINT32_C(10000)
#define BOOT_TRANSFER_TIMEOUT_MS      UINT32_C(5000)
#define BOOT_J1939_ADDRESS_CLAIM_MS   UINT32_C(250)
#define BOOT_J1939_CANNOT_CLAIM_MAX_MS UINT32_C(153)
#define BOOT_J1939_TP_TIMEOUT_MS      UINT32_C(1250)
#define BOOT_RUN_RESPONSE_DELAY_MS    UINT32_C(50)
#define BOOT_RUN_RESPONSE_TIMEOUT_MS  UINT32_C(250)
#define BOOT_LED_FAST_MS              UINT32_C(125)
#define BOOT_LED_WAIT_MS              UINT32_C(500)
#define BOOT_LED_ERROR_MS             UINT32_C(100)
#define BOOT_METADATA_MAGIC           UINT32_C(0x444D4C43) /* "CLMD" in little endian. */
#define BOOT_METADATA_FORMAT_VERSION  UINT16_C(1)

#if (BOOT_J1939_DEFAULT_PREFERRED_ADDRESS >= BOOT_J1939_NULL_ADDRESS)
#error "The preferred J1939 source address must be in the range 0..253"
#endif

#if (BOOT_J1939_NAME_MANUFACTURER_CODE > 0x7FFU) || \
    (BOOT_J1939_NAME_ECU_INSTANCE > 0x07U) || \
    (BOOT_J1939_NAME_FUNCTION_INSTANCE > 0x1FU) || \
    (BOOT_J1939_NAME_VEHICLE_SYSTEM > 0x7FU) || \
    (BOOT_J1939_NAME_VEHICLE_SYSTEM_INSTANCE > 0x0FU) || \
    (BOOT_J1939_NAME_INDUSTRY_GROUP > 0x07U) || \
    (BOOT_J1939_NAME_ARBITRARY_ADDRESS_CAPABLE > 1U)
#error "One or more J1939 NAME fields are outside their encoded width"
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
