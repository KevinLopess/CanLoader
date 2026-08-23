#ifndef BOOT_J1939_ADDRESS_CLAIM_H
#define BOOT_J1939_ADDRESS_CLAIM_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_can.h"

typedef enum
{
    BOOT_J1939_ADDRESS_INITIAL = 0,
    BOOT_J1939_ADDRESS_CLAIMING,
    BOOT_J1939_ADDRESS_CLAIMED,
    BOOT_J1939_ADDRESS_WAIT_CANNOT_CLAIM,
    BOOT_J1939_ADDRESS_CANNOT_CLAIM
} boot_j1939_address_state_t;

typedef struct
{
    uint64_t name;
    uint32_t deadline_ms;
    uint8_t preferred_address;
    uint8_t current_address;
    uint8_t next_arbitrary_address;
    uint8_t arbitrary_attempts;
    boot_j1939_address_state_t state;
    bool tx_pending;
    bool address_changed;
    boot_can_frame_t pending_tx;
} boot_j1939_address_claim_t;

void boot_j1939_address_claim_init(boot_j1939_address_claim_t *claim,
                                   uint64_t name,
                                   uint8_t preferred_address);
bool boot_j1939_address_claim_start(boot_j1939_address_claim_t *claim,
                                    uint32_t now_ms);
bool boot_j1939_address_claim_restart(boot_j1939_address_claim_t *claim,
                                      uint8_t preferred_address,
                                      uint32_t now_ms);
bool boot_j1939_address_claim_handle(boot_j1939_address_claim_t *claim,
                                     const boot_can_frame_t *frame,
                                     uint32_t now_ms);
void boot_j1939_address_claim_task(boot_j1939_address_claim_t *claim,
                                   uint32_t now_ms);
bool boot_j1939_address_claim_take_tx(boot_j1939_address_claim_t *claim,
                                      boot_can_frame_t *frame);
bool boot_j1939_address_claim_take_address_changed(
    boot_j1939_address_claim_t *claim,
    uint8_t *address);
bool boot_j1939_address_claim_is_operational(
    const boot_j1939_address_claim_t *claim);
uint8_t boot_j1939_address_claim_source_address(
    const boot_j1939_address_claim_t *claim);
boot_j1939_address_state_t boot_j1939_address_claim_state(
    const boot_j1939_address_claim_t *claim);

#endif /* BOOT_J1939_ADDRESS_CLAIM_H */
