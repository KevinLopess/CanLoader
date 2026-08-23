#ifndef BOOT_J1939_H
#define BOOT_J1939_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_can.h"

#define BOOT_J1939_PGN_REQUEST            UINT32_C(0x00EA00)
#define BOOT_J1939_PGN_ADDRESS_CLAIMED    UINT32_C(0x00EE00)
#define BOOT_J1939_PGN_TP_CM              UINT32_C(0x00EC00)
#define BOOT_J1939_PGN_TP_DT              UINT32_C(0x00EB00)
#define BOOT_J1939_PGN_DM14               UINT32_C(0x00D900)
#define BOOT_J1939_PGN_DM15               UINT32_C(0x00D800)
#define BOOT_J1939_PGN_DM16               UINT32_C(0x00D700)
#define BOOT_J1939_PGN_PROPRIETARY_A      UINT32_C(0x00EF00)
#define BOOT_J1939_PGN_COMMANDED_ADDRESS  UINT32_C(0x00FED8)
#define BOOT_J1939_MAX_PGN                UINT32_C(0x01FFFF)

typedef struct
{
    uint32_t pgn;
    uint8_t priority;
    uint8_t source_address;
    uint8_t destination_address;
    bool destination_specific;
} boot_j1939_header_t;

bool boot_j1939_make_id(uint8_t priority,
                        uint32_t pgn,
                        uint8_t source_address,
                        uint8_t destination_address,
                        uint32_t *identifier);
bool boot_j1939_decode_id(uint32_t identifier, boot_j1939_header_t *header);
bool boot_j1939_decode_frame(const boot_can_frame_t *frame,
                             boot_j1939_header_t *header);
bool boot_j1939_make_frame(boot_can_frame_t *frame,
                           uint8_t priority,
                           uint32_t pgn,
                           uint8_t source_address,
                           uint8_t destination_address,
                           const uint8_t *data,
                           uint8_t dlc);

#endif /* BOOT_J1939_H */
