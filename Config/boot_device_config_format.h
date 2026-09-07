#ifndef BOOT_DEVICE_CONFIG_FORMAT_H
#define BOOT_DEVICE_CONFIG_FORMAT_H

#include <stdint.h>

/*
 * Append-only journal. The Flash program unit containing the commit halfword
 * is programmed last, so a reset during an update cannot make a partial
 * record valid. On 64-bit Flash targets that final unit also contains the CRC.
 */
#define BOOT_DEVICE_CONFIG_MAGIC                 UINT32_C(0x46434C43) /* "CLCF" */
#define BOOT_DEVICE_CONFIG_FORMAT_VERSION        UINT16_C(2)
#define BOOT_DEVICE_CONFIG_RECORD_SIZE           32U
#define BOOT_DEVICE_CONFIG_CRC_INPUT_SIZE        24U
#define BOOT_DEVICE_CONFIG_COMMIT_VALUE          UINT16_C(0x0000)

#define BOOT_DEVICE_CONFIG_MAGIC_OFFSET          0U
#define BOOT_DEVICE_CONFIG_FORMAT_OFFSET         4U
#define BOOT_DEVICE_CONFIG_SIZE_OFFSET           6U
#define BOOT_DEVICE_CONFIG_GENERATION_OFFSET     8U
#define BOOT_DEVICE_CONFIG_NAME_OFFSET           12U
#define BOOT_DEVICE_CONFIG_ADDRESS_OFFSET        20U
#define BOOT_DEVICE_CONFIG_ADDRESS_INV_OFFSET    21U
#define BOOT_DEVICE_CONFIG_FLAGS_OFFSET          22U
#define BOOT_DEVICE_CONFIG_CRC_OFFSET            24U
#define BOOT_DEVICE_CONFIG_COMMIT_OFFSET         28U
#define BOOT_DEVICE_CONFIG_RESERVED_OFFSET       30U

#define BOOT_DEVICE_CONFIG_FLAG_NAME_PROVISIONED UINT16_C(0x0001)

#endif /* BOOT_DEVICE_CONFIG_FORMAT_H */
