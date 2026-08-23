#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "boot_config.h"
#include "boot_j1939.h"
#include "boot_j1939_address_claim.h"
#include "boot_protocol.h"

#define OWN_NAME UINT64_C(0x8000000000000100)
#define OWN_SA   UINT8_C(0x80)
#define TOOL_SA  UINT8_C(0x90)

static boot_can_frame_t make_address_claim(uint8_t source, uint64_t name)
{
    boot_can_frame_t frame;
    uint8_t payload[8];

    boot_protocol_write_u64_le(payload, name);
    assert(boot_j1939_make_frame(&frame,
                                 BOOT_J1939_PRIORITY,
                                 BOOT_J1939_PGN_ADDRESS_CLAIMED,
                                 source,
                                 BOOT_J1939_GLOBAL_ADDRESS,
                                 payload,
                                 sizeof(payload)));
    return frame;
}

static boot_can_frame_t make_request(uint8_t destination)
{
    boot_can_frame_t frame;
    uint8_t payload[3];

    boot_protocol_write_u24_le(payload, BOOT_J1939_PGN_ADDRESS_CLAIMED);
    assert(boot_j1939_make_frame(&frame,
                                 BOOT_J1939_PRIORITY,
                                 BOOT_J1939_PGN_REQUEST,
                                 TOOL_SA,
                                 destination,
                                 payload,
                                 sizeof(payload)));
    return frame;
}

static void assert_claim_frame(const boot_can_frame_t *frame,
                               uint8_t source,
                               uint64_t name)
{
    boot_j1939_header_t header;

    assert(boot_j1939_decode_frame(frame, &header));
    assert(header.pgn == BOOT_J1939_PGN_ADDRESS_CLAIMED);
    assert(header.source_address == source);
    assert(header.destination_address == BOOT_J1939_GLOBAL_ADDRESS);
    assert(frame->dlc == 8U);
    assert(boot_protocol_read_u64_le(frame->data) == name);
}

static void test_initial_claim_and_request(void)
{
    boot_j1939_address_claim_t claim;
    boot_can_frame_t frame;
    uint8_t address;

    boot_j1939_address_claim_init(&claim, OWN_NAME, OWN_SA);
    assert(boot_j1939_address_claim_state(&claim) ==
           BOOT_J1939_ADDRESS_INITIAL);
    assert(boot_j1939_address_claim_start(&claim, 100U));
    assert(boot_j1939_address_claim_take_address_changed(&claim, &address));
    assert(address == OWN_SA);
    assert(boot_j1939_address_claim_take_tx(&claim, &frame));
    assert_claim_frame(&frame, OWN_SA, OWN_NAME);

    boot_j1939_address_claim_task(&claim, 349U);
    assert(!boot_j1939_address_claim_is_operational(&claim));
    boot_j1939_address_claim_task(&claim, 350U);
    assert(boot_j1939_address_claim_is_operational(&claim));

    frame = make_request(BOOT_J1939_GLOBAL_ADDRESS);
    assert(boot_j1939_address_claim_handle(&claim, &frame, 400U));
    assert(boot_j1939_address_claim_take_tx(&claim, &frame));
    assert_claim_frame(&frame, OWN_SA, OWN_NAME);

    frame = make_request(UINT8_C(0x91));
    assert(!boot_j1939_address_claim_handle(&claim, &frame, 401U));
}

static void test_name_arbitration(void)
{
    boot_j1939_address_claim_t claim;
    boot_can_frame_t frame;
    uint8_t address;

    boot_j1939_address_claim_init(&claim, OWN_NAME, OWN_SA);
    assert(boot_j1939_address_claim_start(&claim, 0U));
    assert(boot_j1939_address_claim_take_tx(&claim, &frame));
    (void)boot_j1939_address_claim_take_address_changed(&claim, &address);

    frame = make_address_claim(OWN_SA, OWN_NAME + 1U);
    assert(boot_j1939_address_claim_handle(&claim, &frame, 10U));
    assert(boot_j1939_address_claim_source_address(&claim) == OWN_SA);
    assert(boot_j1939_address_claim_take_tx(&claim, &frame));
    assert_claim_frame(&frame, OWN_SA, OWN_NAME);

    frame = make_address_claim(OWN_SA, OWN_NAME - 1U);
    assert(boot_j1939_address_claim_handle(&claim, &frame, 20U));
    assert(boot_j1939_address_claim_source_address(&claim) == UINT8_C(0x81));
    assert(boot_j1939_address_claim_take_address_changed(&claim, &address));
    assert(address == UINT8_C(0x81));
    assert(boot_j1939_address_claim_take_tx(&claim, &frame));
    assert_claim_frame(&frame, UINT8_C(0x81), OWN_NAME);

    boot_j1939_address_claim_task(&claim, 269U);
    assert(!boot_j1939_address_claim_is_operational(&claim));
    boot_j1939_address_claim_task(&claim, 270U);
    assert(boot_j1939_address_claim_is_operational(&claim));
}

static void test_non_arbitrary_device_cannot_claim(void)
{
    boot_j1939_address_claim_t claim;
    boot_can_frame_t frame;
    uint8_t address;
    const uint64_t fixed_name = OWN_NAME & ~(UINT64_C(1) << 63U);

    boot_j1939_address_claim_init(&claim, fixed_name, OWN_SA);
    assert(boot_j1939_address_claim_start(&claim, 1000U));
    assert(boot_j1939_address_claim_take_tx(&claim, &frame));
    (void)boot_j1939_address_claim_take_address_changed(&claim, &address);

    frame = make_address_claim(OWN_SA, fixed_name - 1U);
    assert(boot_j1939_address_claim_handle(&claim, &frame, 1010U));
    assert(boot_j1939_address_claim_state(&claim) ==
           BOOT_J1939_ADDRESS_WAIT_CANNOT_CLAIM);
    assert(boot_j1939_address_claim_take_address_changed(&claim, &address));
    assert(address == BOOT_J1939_NULL_ADDRESS);

    boot_j1939_address_claim_task(
        &claim,
        1010U + BOOT_J1939_CANNOT_CLAIM_MAX_MS);
    assert(boot_j1939_address_claim_state(&claim) ==
           BOOT_J1939_ADDRESS_CANNOT_CLAIM);
    assert(boot_j1939_address_claim_take_tx(&claim, &frame));
    assert_claim_frame(&frame, BOOT_J1939_NULL_ADDRESS, fixed_name);
}

static void test_restart_and_invalid_arguments(void)
{
    boot_j1939_address_claim_t claim;
    boot_can_frame_t frame;

    boot_j1939_address_claim_init(&claim, 0U, OWN_SA);
    assert(!boot_j1939_address_claim_start(&claim, 0U));
    boot_j1939_address_claim_init(&claim, OWN_NAME, BOOT_J1939_NULL_ADDRESS);
    assert(!boot_j1939_address_claim_start(&claim, 0U));

    boot_j1939_address_claim_init(&claim, OWN_NAME, OWN_SA);
    assert(boot_j1939_address_claim_start(&claim, 0U));
    assert(boot_j1939_address_claim_take_tx(&claim, &frame));
    assert(boot_j1939_address_claim_restart(&claim, UINT8_C(0x82), 50U));
    assert(boot_j1939_address_claim_take_tx(&claim, &frame));
    assert_claim_frame(&frame, UINT8_C(0x82), OWN_NAME);
}

static void test_ignores_nonconformant_claim_and_request_sources(void)
{
    boot_j1939_address_claim_t claim;
    boot_can_frame_t frame;
    uint8_t payload[8];

    boot_j1939_address_claim_init(&claim, OWN_NAME, OWN_SA);
    assert(boot_j1939_address_claim_start(&claim, 0U));
    assert(boot_j1939_address_claim_take_tx(&claim, &frame));

    boot_protocol_write_u64_le(payload, OWN_NAME - 1U);
    assert(boot_j1939_make_frame(&frame,
                                 BOOT_J1939_PRIORITY,
                                 BOOT_J1939_PGN_ADDRESS_CLAIMED,
                                 OWN_SA,
                                 UINT8_C(0x81),
                                 payload,
                                 sizeof(payload)));
    assert(!boot_j1939_address_claim_handle(&claim, &frame, 1U));
    assert(boot_j1939_address_claim_source_address(&claim) == OWN_SA);

    frame = make_request(BOOT_J1939_GLOBAL_ADDRESS);
    frame.id = (frame.id & ~UINT32_C(0xFF)) | BOOT_J1939_NULL_ADDRESS;
    assert(!boot_j1939_address_claim_handle(&claim, &frame, 2U));
    assert(!boot_j1939_address_claim_take_tx(&claim, &frame));
}

int main(void)
{
    test_initial_claim_and_request();
    test_name_arbitration();
    test_non_arbitrary_device_cannot_claim();
    test_restart_and_invalid_arguments();
    test_ignores_nonconformant_claim_and_request_sources();

    puts("test_j1939_address_claim: OK");
    return 0;
}
