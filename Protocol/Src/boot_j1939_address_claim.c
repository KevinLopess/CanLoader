#include "boot_j1939_address_claim.h"

#include <stddef.h>

#include "boot_config.h"
#include "boot_j1939.h"
#include "boot_protocol.h"

#define J1939_NAME_AAC_BIT       (UINT64_C(1) << 63U)
#define J1939_ARBITRARY_COUNT    ((uint8_t)(BOOT_J1939_ARBITRARY_ADDRESS_MAX - \
                                            BOOT_J1939_ARBITRARY_ADDRESS_MIN + 1U))

static bool deadline_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static bool address_valid(uint8_t address)
{
    return address < BOOT_J1939_NULL_ADDRESS;
}

static bool queue_address_claim(boot_j1939_address_claim_t *claim,
                                uint8_t source_address)
{
    uint8_t payload[BOOT_CAN_MAX_DATA_LENGTH];

    if ((claim == NULL) || claim->tx_pending)
    {
        return false;
    }

    boot_protocol_write_u64_le(payload, claim->name);
    if (!boot_j1939_make_frame(&claim->pending_tx,
                                BOOT_J1939_PRIORITY,
                                BOOT_J1939_PGN_ADDRESS_CLAIMED,
                                source_address,
                                BOOT_J1939_GLOBAL_ADDRESS,
                                payload,
                                BOOT_CAN_MAX_DATA_LENGTH))
    {
        return false;
    }

    claim->tx_pending = true;
    return true;
}

static void set_current_address(boot_j1939_address_claim_t *claim,
                                uint8_t address)
{
    if (claim->current_address != address)
    {
        claim->current_address = address;
        claim->address_changed = true;
    }
}

static void schedule_cannot_claim(boot_j1939_address_claim_t *claim,
                                  uint32_t now_ms)
{
    const uint32_t pseudo_random =
        (uint32_t)((claim->name ^ (claim->name >> 32U) ^ now_ms) %
                   (BOOT_J1939_CANNOT_CLAIM_MAX_MS + 1U));

    set_current_address(claim, BOOT_J1939_NULL_ADDRESS);
    claim->state = BOOT_J1939_ADDRESS_WAIT_CANNOT_CLAIM;
    claim->deadline_ms = now_ms + pseudo_random;
}

static bool start_claiming(boot_j1939_address_claim_t *claim,
                           uint8_t address,
                           uint32_t now_ms)
{
    if (!address_valid(address))
    {
        return false;
    }

    set_current_address(claim, address);
    claim->state = BOOT_J1939_ADDRESS_CLAIMING;
    claim->deadline_ms = now_ms + BOOT_J1939_ADDRESS_CLAIM_MS;
    return queue_address_claim(claim, address);
}

static void claim_next_arbitrary_address(boot_j1939_address_claim_t *claim,
                                         uint32_t now_ms)
{
    uint8_t candidate;

    if (((claim->name & J1939_NAME_AAC_BIT) == 0U) ||
        (claim->arbitrary_attempts >= J1939_ARBITRARY_COUNT))
    {
        schedule_cannot_claim(claim, now_ms);
        return;
    }

    candidate = claim->next_arbitrary_address;
    if ((candidate < BOOT_J1939_ARBITRARY_ADDRESS_MIN) ||
        (candidate > BOOT_J1939_ARBITRARY_ADDRESS_MAX))
    {
        candidate = BOOT_J1939_ARBITRARY_ADDRESS_MIN;
    }
    claim->next_arbitrary_address =
        (candidate == BOOT_J1939_ARBITRARY_ADDRESS_MAX) ?
        BOOT_J1939_ARBITRARY_ADDRESS_MIN : (uint8_t)(candidate + 1U);
    ++claim->arbitrary_attempts;

    if (!start_claiming(claim, candidate, now_ms))
    {
        schedule_cannot_claim(claim, now_ms);
    }
}

void boot_j1939_address_claim_init(boot_j1939_address_claim_t *claim,
                                   uint64_t name,
                                   uint8_t preferred_address)
{
    if (claim == NULL)
    {
        return;
    }

    *claim = (boot_j1939_address_claim_t){0};
    claim->name = name;
    claim->preferred_address = preferred_address;
    claim->current_address = BOOT_J1939_NULL_ADDRESS;
    claim->next_arbitrary_address =
        ((preferred_address >= BOOT_J1939_ARBITRARY_ADDRESS_MIN) &&
         (preferred_address < BOOT_J1939_ARBITRARY_ADDRESS_MAX)) ?
        (uint8_t)(preferred_address + 1U) : BOOT_J1939_ARBITRARY_ADDRESS_MIN;
    claim->state = BOOT_J1939_ADDRESS_INITIAL;
}

bool boot_j1939_address_claim_start(boot_j1939_address_claim_t *claim,
                                    uint32_t now_ms)
{
    if ((claim == NULL) || (claim->name == 0U) ||
        (claim->name == UINT64_MAX) ||
        !address_valid(claim->preferred_address))
    {
        return false;
    }

    claim->arbitrary_attempts = 0U;
    claim->tx_pending = false;
    return start_claiming(claim, claim->preferred_address, now_ms);
}

bool boot_j1939_address_claim_restart(boot_j1939_address_claim_t *claim,
                                      uint8_t preferred_address,
                                      uint32_t now_ms)
{
    if ((claim == NULL) || !address_valid(preferred_address))
    {
        return false;
    }

    claim->preferred_address = preferred_address;
    claim->next_arbitrary_address =
        ((preferred_address >= BOOT_J1939_ARBITRARY_ADDRESS_MIN) &&
         (preferred_address < BOOT_J1939_ARBITRARY_ADDRESS_MAX)) ?
        (uint8_t)(preferred_address + 1U) : BOOT_J1939_ARBITRARY_ADDRESS_MIN;
    claim->state = BOOT_J1939_ADDRESS_INITIAL;
    claim->current_address = BOOT_J1939_NULL_ADDRESS;
    claim->address_changed = true;
    return boot_j1939_address_claim_start(claim, now_ms);
}

bool boot_j1939_address_claim_handle(boot_j1939_address_claim_t *claim,
                                     const boot_can_frame_t *frame,
                                     uint32_t now_ms)
{
    boot_j1939_header_t header;

    if ((claim == NULL) || (frame == NULL) ||
        !boot_j1939_decode_frame(frame, &header))
    {
        return false;
    }

    if (header.pgn == BOOT_J1939_PGN_REQUEST)
    {
        if ((frame->dlc < 3U) ||
            (header.source_address >= BOOT_J1939_NULL_ADDRESS) ||
            (boot_protocol_read_u24_le(frame->data) !=
             BOOT_J1939_PGN_ADDRESS_CLAIMED) ||
            ((header.destination_address != BOOT_J1939_GLOBAL_ADDRESS) &&
             ((claim->current_address >= BOOT_J1939_NULL_ADDRESS) ||
              (header.destination_address != claim->current_address))))
        {
            return false;
        }

        if (claim->current_address == BOOT_J1939_NULL_ADDRESS)
        {
            schedule_cannot_claim(claim, now_ms);
        }
        else
        {
            (void)queue_address_claim(claim, claim->current_address);
        }
        return true;
    }

    if ((header.pgn != BOOT_J1939_PGN_ADDRESS_CLAIMED) ||
        (header.destination_address != BOOT_J1939_GLOBAL_ADDRESS) ||
        (frame->dlc != BOOT_CAN_MAX_DATA_LENGTH))
    {
        return false;
    }

    if ((claim->current_address == BOOT_J1939_NULL_ADDRESS) ||
        (header.source_address != claim->current_address))
    {
        return true;
    }

    {
        const uint64_t received_name = boot_protocol_read_u64_le(frame->data);

        if (received_name < claim->name)
        {
            claim->tx_pending = false;
            claim_next_arbitrary_address(claim, now_ms);
        }
        else if (received_name > claim->name)
        {
            (void)queue_address_claim(claim, claim->current_address);
            claim->state = BOOT_J1939_ADDRESS_CLAIMING;
            claim->deadline_ms = now_ms + BOOT_J1939_ADDRESS_CLAIM_MS;
        }
    }
    return true;
}

void boot_j1939_address_claim_task(boot_j1939_address_claim_t *claim,
                                   uint32_t now_ms)
{
    if (claim == NULL)
    {
        return;
    }

    if ((claim->state == BOOT_J1939_ADDRESS_CLAIMING) &&
        deadline_reached(now_ms, claim->deadline_ms))
    {
        claim->state = BOOT_J1939_ADDRESS_CLAIMED;
    }
    else if ((claim->state == BOOT_J1939_ADDRESS_WAIT_CANNOT_CLAIM) &&
             deadline_reached(now_ms, claim->deadline_ms) &&
             !claim->tx_pending)
    {
        if (queue_address_claim(claim, BOOT_J1939_NULL_ADDRESS))
        {
            claim->state = BOOT_J1939_ADDRESS_CANNOT_CLAIM;
        }
    }
}

bool boot_j1939_address_claim_take_tx(boot_j1939_address_claim_t *claim,
                                      boot_can_frame_t *frame)
{
    if ((claim == NULL) || (frame == NULL) || !claim->tx_pending)
    {
        return false;
    }

    *frame = claim->pending_tx;
    claim->tx_pending = false;
    return true;
}

bool boot_j1939_address_claim_take_address_changed(
    boot_j1939_address_claim_t *claim,
    uint8_t *address)
{
    if ((claim == NULL) || (address == NULL) || !claim->address_changed)
    {
        return false;
    }

    *address = claim->current_address;
    claim->address_changed = false;
    return true;
}

bool boot_j1939_address_claim_is_operational(
    const boot_j1939_address_claim_t *claim)
{
    return (claim != NULL) && (claim->state == BOOT_J1939_ADDRESS_CLAIMED);
}

uint8_t boot_j1939_address_claim_source_address(
    const boot_j1939_address_claim_t *claim)
{
    return (claim == NULL) ? BOOT_J1939_NULL_ADDRESS : claim->current_address;
}

boot_j1939_address_state_t boot_j1939_address_claim_state(
    const boot_j1939_address_claim_t *claim)
{
    return (claim == NULL) ? BOOT_J1939_ADDRESS_INITIAL : claim->state;
}
