#ifndef BOOT_TARGET_STM32G0B1RE_H
#define BOOT_TARGET_STM32G0B1RE_H

/*
 * NUCLEO-G0B1RE / STM32G0B1RE: 512 KiB Flash and 144 KiB SRAM.
 * The 32 KiB boot partition contains 30 KiB of code followed by one
 * 2 KiB device-configuration page. The final Flash page stores metadata.
 */
#define BOOT_FLASH_BASE_ADDRESS       UINT32_C(0x08000000)
#define BOOT_FLASH_TOTAL_SIZE         UINT32_C(0x00080000)
#define BOOTLOADER_FLASH_SIZE         UINT32_C(0x00008000)
#define BOOTLOADER_CODE_SIZE          UINT32_C(0x00007800)
#define BOOT_DEVICE_CONFIG_PAGE_SIZE  UINT32_C(0x00000800)
#define BOOT_APPLICATION_ADDRESS      (BOOT_FLASH_BASE_ADDRESS + BOOTLOADER_FLASH_SIZE)
#define BOOT_METADATA_PAGE_SIZE       UINT32_C(0x00000800)

#define BOOT_SRAM_START_ADDRESS       UINT32_C(0x20000000)
#define BOOT_SRAM_SIZE                UINT32_C(0x00024000)

#define BOOT_FLASH_PAGE_SIZE          UINT32_C(2048)
#define BOOT_FLASH_PROGRAM_UNIT       UINT32_C(8)
/* The target advertises this value in START_UPDATE; the host accepts <=100. */
#define BOOT_TRANSFER_BLOCK_SIZE      UINT32_C(96)
#define BOOT_TARGET_CODE              UINT8_C(0xB1)
#define BOOT_LED_ACTIVE_LOW           0U

#endif /* BOOT_TARGET_STM32G0B1RE_H */
