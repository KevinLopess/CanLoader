#include "boot_j1939.h"

#include <stddef.h>

#include "boot_config.h"

#define J1939_PRIORITY_SHIFT 26U
#define J1939_PGN_SHIFT      8U
#define J1939_PDU_FORMAT_SHIFT 16U
#define J1939_PDU_SPECIFIC_SHIFT 8U
#define J1939_RESERVED_BIT   UINT32_C(0x02000000)

static bool pgn_is_destination_specific(uint32_t pgn)
{
    return ((pgn >> 8U) & UINT32_C(0xFF)) < UINT32_C(0xF0);
}

bool boot_j1939_make_id(uint8_t priority,
                        uint32_t pgn,
                        uint8_t source_address,
                        uint8_t destination_address,
                        uint32_t *identifier)
{
    uint32_t id;

    if ((identifier == NULL) || (priority > 7U) ||
        (pgn > BOOT_J1939_MAX_PGN) ||
        (source_address > BOOT_J1939_NULL_ADDRESS))
    {
        return false;
    }

    if (pgn_is_destination_specific(pgn))
    {
        if ((pgn & UINT32_C(0xFF)) != 0U)
        {
            return false;
        }
        id = ((uint32_t)priority << J1939_PRIORITY_SHIFT) |
             (pgn << J1939_PGN_SHIFT) |
             ((uint32_t)destination_address << J1939_PDU_SPECIFIC_SHIFT) |
             source_address;
    }
    else
    {
        id = ((uint32_t)priority << J1939_PRIORITY_SHIFT) |
             (pgn << J1939_PGN_SHIFT) |
             source_address;
    }

    if ((id & J1939_RESERVED_BIT) != 0U)
    {
        return false;
    }

    *identifier = id;
    return true;
}

bool boot_j1939_decode_id(uint32_t identifier, boot_j1939_header_t *header)
{
    uint32_t raw_pgn;
    uint8_t pdu_format;
    uint8_t source_address;

    if ((header == NULL) || (identifier > BOOT_CAN_EXTENDED_ID_MAX) ||
        ((identifier & J1939_RESERVED_BIT) != 0U))
    {
        return false;
    }

    raw_pgn = (identifier >> J1939_PGN_SHIFT) & BOOT_J1939_MAX_PGN;
    pdu_format = (uint8_t)(identifier >> J1939_PDU_FORMAT_SHIFT);
    source_address = (uint8_t)identifier;
    if (source_address == BOOT_J1939_GLOBAL_ADDRESS)
    {
        return false;
    }
    header->priority = (uint8_t)((identifier >> J1939_PRIORITY_SHIFT) & 7U);
    header->source_address = source_address;
    header->destination_specific = pdu_format < UINT8_C(0xF0);

    if (header->destination_specific)
    {
        header->destination_address =
            (uint8_t)(identifier >> J1939_PDU_SPECIFIC_SHIFT);
        header->pgn = raw_pgn & UINT32_C(0x01FF00);
    }
    else
    {
        header->destination_address = BOOT_J1939_GLOBAL_ADDRESS;
        header->pgn = raw_pgn;
    }

    return true;
}

bool boot_j1939_decode_frame(const boot_can_frame_t *frame,
                             boot_j1939_header_t *header)
{
    if ((frame == NULL) || (frame->id_type != BOOT_CAN_ID_EXTENDED) ||
        (frame->dlc > BOOT_CAN_MAX_DATA_LENGTH))
    {
        return false;
    }

    return boot_j1939_decode_id(frame->id, header);
}

bool boot_j1939_make_frame(boot_can_frame_t *frame,
                           uint8_t priority,
                           uint32_t pgn,
                           uint8_t source_address,
                           uint8_t destination_address,
                           const uint8_t *data,
                           uint8_t dlc)
{
    uint8_t index;

    if ((frame == NULL) || (dlc > BOOT_CAN_MAX_DATA_LENGTH) ||
        ((data == NULL) && (dlc != 0U)) ||
        !boot_j1939_make_id(priority,
                            pgn,
                            source_address,
                            destination_address,
                            &frame->id))
    {
        return false;
    }

    frame->id_type = BOOT_CAN_ID_EXTENDED;
    frame->dlc = dlc;
    for (index = 0U; index < dlc; ++index)
    {
        frame->data[index] = data[index];
    }
    for (; index < BOOT_CAN_MAX_DATA_LENGTH; ++index)
    {
        frame->data[index] = UINT8_C(0xFF);
    }
    return true;
}
