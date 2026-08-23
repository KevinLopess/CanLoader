#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "boot_config.h"
#include "boot_j1939.h"
#include "boot_j1939_tp.h"
#include "boot_protocol.h"

#define DEVICE_SA UINT8_C(0x80)
#define TOOL_SA   UINT8_C(0x90)

static boot_can_frame_t make_cm(uint8_t source,
                                uint8_t destination,
                                uint8_t control,
                                uint16_t size,
                                uint8_t packets,
                                uint8_t byte_five,
                                uint32_t pgn)
{
    boot_can_frame_t frame;
    uint8_t payload[8] = {
        control, 0U, 0U, packets, byte_five, 0U, 0U, 0U
    };

    boot_protocol_write_u16_le(&payload[1], size);
    boot_protocol_write_u24_le(&payload[5], pgn);
    assert(boot_j1939_make_frame(&frame,
                                 BOOT_J1939_PRIORITY,
                                 BOOT_J1939_PGN_TP_CM,
                                 source,
                                 destination,
                                 payload,
                                 sizeof(payload)));
    return frame;
}

static boot_can_frame_t make_dt(uint8_t source,
                                uint8_t destination,
                                uint8_t sequence,
                                const uint8_t *payload,
                                size_t offset,
                                size_t total_size)
{
    boot_can_frame_t frame;
    uint8_t data[8] = {
        sequence,
        UINT8_C(0xFF), UINT8_C(0xFF), UINT8_C(0xFF),
        UINT8_C(0xFF), UINT8_C(0xFF), UINT8_C(0xFF), UINT8_C(0xFF)
    };
    size_t index;

    for (index = 0U; (index < 7U) && ((offset + index) < total_size); ++index)
    {
        data[index + 1U] = payload[offset + index];
    }
    assert(boot_j1939_make_frame(&frame,
                                 BOOT_J1939_PRIORITY,
                                 BOOT_J1939_PGN_TP_DT,
                                 source,
                                 destination,
                                 data,
                                 sizeof(data)));
    return frame;
}

static void assert_cm_response(const boot_can_frame_t *frame,
                               uint8_t control,
                               uint8_t source,
                               uint8_t destination,
                               uint32_t pgn)
{
    boot_j1939_header_t header;

    assert(boot_j1939_decode_frame(frame, &header));
    assert(header.pgn == BOOT_J1939_PGN_TP_CM);
    assert(header.source_address == source);
    assert(header.destination_address == destination);
    assert(frame->data[0] == control);
    assert(boot_protocol_read_u24_le(&frame->data[5]) == pgn);
}

static void test_rts_cts_complete_two_windows(void)
{
    boot_j1939_tp_rx_t receiver;
    boot_can_frame_t frame;
    boot_can_frame_t response;
    boot_j1939_tp_event_t event;
    bool ready;
    uint8_t input[101];
    uint8_t output[101] = {0U};
    uint8_t sequence;
    size_t index;

    input[0] = UINT8_C(0xFF);
    for (index = 1U; index < sizeof(input); ++index)
    {
        input[index] = (uint8_t)index;
    }

    boot_j1939_tp_rx_init(&receiver);
    assert(boot_j1939_tp_rx_expect_rts(&receiver,
                                       TOOL_SA,
                                       DEVICE_SA,
                                       BOOT_J1939_PGN_DM16,
                                       sizeof(input),
                                       output,
                                       sizeof(output)));
    frame = make_cm(TOOL_SA,
                    DEVICE_SA,
                    BOOT_J1939_TP_CONTROL_RTS,
                    sizeof(input),
                    15U,
                    8U,
                    BOOT_J1939_PGN_DM16);
    assert(boot_j1939_tp_rx_handle(&receiver,
                                   &frame,
                                   100U,
                                   &response,
                                   &ready,
                                   &event));
    assert(ready);
    assert(event == BOOT_J1939_TP_EVENT_STARTED);
    assert_cm_response(&response,
                       BOOT_J1939_TP_CONTROL_CTS,
                       DEVICE_SA,
                       TOOL_SA,
                       BOOT_J1939_PGN_DM16);
    assert(response.data[1] == 8U);
    assert(response.data[2] == 1U);

    for (sequence = 1U; sequence <= 15U; ++sequence)
    {
        const size_t offset = (size_t)(sequence - 1U) * 7U;

        frame = make_dt(TOOL_SA,
                        DEVICE_SA,
                        sequence,
                        input,
                        offset,
                        sizeof(input));
        assert(boot_j1939_tp_rx_handle(&receiver,
                                       &frame,
                                       100U + sequence,
                                       &response,
                                       &ready,
                                       &event));
        if (sequence == 8U)
        {
            assert(ready);
            assert(event == BOOT_J1939_TP_EVENT_NONE);
            assert_cm_response(&response,
                               BOOT_J1939_TP_CONTROL_CTS,
                               DEVICE_SA,
                               TOOL_SA,
                               BOOT_J1939_PGN_DM16);
            assert(response.data[1] == 7U);
            assert(response.data[2] == 9U);
        }
        else if (sequence == 15U)
        {
            assert(ready);
            assert(event == BOOT_J1939_TP_EVENT_COMPLETE);
            assert_cm_response(&response,
                               BOOT_J1939_TP_CONTROL_EOM_ACK,
                               DEVICE_SA,
                               TOOL_SA,
                               BOOT_J1939_PGN_DM16);
            assert(boot_protocol_read_u16_le(&response.data[1]) == sizeof(input));
            assert(response.data[3] == 15U);
        }
        else
        {
            assert(!ready);
            assert(event == BOOT_J1939_TP_EVENT_NONE);
        }
    }

    assert(boot_j1939_tp_rx_size(&receiver) == sizeof(input));
    assert(boot_j1939_tp_rx_pgn(&receiver) == BOOT_J1939_PGN_DM16);
    assert(boot_j1939_tp_rx_source(&receiver) == TOOL_SA);
    assert(memcmp(boot_j1939_tp_rx_data(&receiver), input, sizeof(input)) == 0);
}

static void test_sequence_error_and_timeout(void)
{
    boot_j1939_tp_rx_t receiver;
    boot_can_frame_t frame;
    boot_can_frame_t response;
    boot_j1939_tp_event_t event;
    bool ready;
    uint8_t buffer[101];
    uint8_t input[101] = {UINT8_C(0xFF)};

    boot_j1939_tp_rx_init(&receiver);
    assert(boot_j1939_tp_rx_expect_rts(&receiver,
                                       TOOL_SA,
                                       DEVICE_SA,
                                       BOOT_J1939_PGN_DM16,
                                       sizeof(buffer),
                                       buffer,
                                       sizeof(buffer)));
    frame = make_cm(TOOL_SA,
                    DEVICE_SA,
                    BOOT_J1939_TP_CONTROL_RTS,
                    sizeof(buffer),
                    15U,
                    8U,
                    BOOT_J1939_PGN_DM16);
    assert(boot_j1939_tp_rx_handle(&receiver,
                                   &frame,
                                   0U,
                                   &response,
                                   &ready,
                                   &event));
    frame = make_dt(TOOL_SA, DEVICE_SA, 2U, input, 0U, sizeof(input));
    assert(boot_j1939_tp_rx_handle(&receiver,
                                   &frame,
                                   1U,
                                   &response,
                                   &ready,
                                   &event));
    assert(ready);
    assert(event == BOOT_J1939_TP_EVENT_ABORTED);
    assert_cm_response(&response,
                       BOOT_J1939_TP_CONTROL_ABORT,
                       DEVICE_SA,
                       TOOL_SA,
                       BOOT_J1939_PGN_DM16);

    assert(boot_j1939_tp_rx_expect_rts(&receiver,
                                       TOOL_SA,
                                       DEVICE_SA,
                                       BOOT_J1939_PGN_DM16,
                                       sizeof(buffer),
                                       buffer,
                                       sizeof(buffer)));
    frame = make_cm(TOOL_SA,
                    DEVICE_SA,
                    BOOT_J1939_TP_CONTROL_RTS,
                    sizeof(buffer),
                    15U,
                    8U,
                    BOOT_J1939_PGN_DM16);
    assert(boot_j1939_tp_rx_handle(&receiver,
                                   &frame,
                                   10U,
                                   &response,
                                   &ready,
                                   &event));
    boot_j1939_tp_rx_task(&receiver,
                          10U + BOOT_J1939_TP_TIMEOUT_MS,
                          &response,
                          &ready,
                          &event);
    assert(ready);
    assert(event == BOOT_J1939_TP_EVENT_TIMEOUT);
    assert_cm_response(&response,
                       BOOT_J1939_TP_CONTROL_ABORT,
                       DEVICE_SA,
                       TOOL_SA,
                       BOOT_J1939_PGN_DM16);
    assert(response.data[1] == BOOT_J1939_TP_ABORT_TIMEOUT);
}

static void test_rejects_unexpected_rts(void)
{
    boot_j1939_tp_rx_t receiver;
    boot_can_frame_t frame;
    boot_can_frame_t response;
    boot_j1939_tp_event_t event;
    bool ready;

    boot_j1939_tp_rx_init(&receiver);
    frame = make_cm(TOOL_SA,
                    DEVICE_SA,
                    BOOT_J1939_TP_CONTROL_RTS,
                    101U,
                    15U,
                    8U,
                    BOOT_J1939_PGN_DM16);
    assert(!boot_j1939_tp_rx_handle(&receiver,
                                    &frame,
                                    0U,
                                    &response,
                                    &ready,
                                    &event));
    assert(!ready);
    assert(event == BOOT_J1939_TP_EVENT_NONE);

    {
        uint8_t buffer[101];

        assert(boot_j1939_tp_rx_expect_rts(&receiver,
                                           TOOL_SA,
                                           DEVICE_SA,
                                           BOOT_J1939_PGN_DM16,
                                           sizeof(buffer),
                                           buffer,
                                           sizeof(buffer)));
        frame = make_cm(TOOL_SA,
                        UINT8_C(0x81),
                        BOOT_J1939_TP_CONTROL_RTS,
                        sizeof(buffer),
                        15U,
                        8U,
                        BOOT_J1939_PGN_DM16);
        assert(!boot_j1939_tp_rx_handle(&receiver,
                                        &frame,
                                        1U,
                                        &response,
                                        &ready,
                                        &event));
        assert(!ready);

        frame = make_cm(UINT8_C(0x91),
                        DEVICE_SA,
                        BOOT_J1939_TP_CONTROL_RTS,
                        sizeof(buffer),
                        15U,
                        8U,
                        BOOT_J1939_PGN_DM16);
        assert(boot_j1939_tp_rx_handle(&receiver,
                                       &frame,
                                       2U,
                                       &response,
                                       &ready,
                                       &event) == false);
        assert(!ready);
        assert(event == BOOT_J1939_TP_EVENT_NONE);

        frame = make_cm(TOOL_SA,
                        DEVICE_SA,
                        BOOT_J1939_TP_CONTROL_RTS,
                        sizeof(buffer),
                        15U,
                        8U,
                        BOOT_J1939_PGN_COMMANDED_ADDRESS);
        assert(!boot_j1939_tp_rx_handle(&receiver,
                                        &frame,
                                        3U,
                                        &response,
                                        &ready,
                                        &event));
        assert(!ready);
        assert(event == BOOT_J1939_TP_EVENT_NONE);

        frame = make_cm(TOOL_SA,
                        DEVICE_SA,
                        BOOT_J1939_TP_CONTROL_RTS,
                        sizeof(buffer) - 1U,
                        15U,
                        8U,
                        BOOT_J1939_PGN_DM16);
        assert(boot_j1939_tp_rx_handle(&receiver,
                                       &frame,
                                       4U,
                                       &response,
                                       &ready,
                                       &event));
        assert(ready);
        assert(event == BOOT_J1939_TP_EVENT_REJECTED);
        assert(boot_j1939_tp_rx_pgn(&receiver) == BOOT_J1939_PGN_DM16);
        assert(boot_j1939_tp_rx_source(&receiver) == TOOL_SA);
        assert_cm_response(&response,
                           BOOT_J1939_TP_CONTROL_ABORT,
                           DEVICE_SA,
                           TOOL_SA,
                           BOOT_J1939_PGN_DM16);
    }
}

static void test_ignores_foreign_transfer_while_active(void)
{
    boot_j1939_tp_rx_t receiver;
    boot_can_frame_t frame;
    boot_can_frame_t response;
    boot_j1939_tp_event_t event;
    bool ready;
    uint8_t buffer[101] = {0U};
    uint8_t input[101] = {UINT8_C(0xFF)};

    boot_j1939_tp_rx_init(&receiver);
    assert(boot_j1939_tp_rx_expect_rts(&receiver,
                                       TOOL_SA,
                                       DEVICE_SA,
                                       BOOT_J1939_PGN_DM16,
                                       sizeof(buffer),
                                       buffer,
                                       sizeof(buffer)));
    frame = make_cm(TOOL_SA,
                    DEVICE_SA,
                    BOOT_J1939_TP_CONTROL_RTS,
                    sizeof(buffer),
                    15U,
                    8U,
                    BOOT_J1939_PGN_DM16);
    assert(boot_j1939_tp_rx_handle(&receiver,
                                   &frame,
                                   0U,
                                   &response,
                                   &ready,
                                   &event));
    assert(ready);
    assert(event == BOOT_J1939_TP_EVENT_STARTED);

    frame = make_cm(UINT8_C(0x91),
                    DEVICE_SA,
                    BOOT_J1939_TP_CONTROL_RTS,
                    sizeof(buffer),
                    15U,
                    8U,
                    BOOT_J1939_PGN_DM16);
    assert(!boot_j1939_tp_rx_handle(&receiver,
                                    &frame,
                                    1U,
                                    &response,
                                    &ready,
                                    &event));
    assert(!ready);
    assert(event == BOOT_J1939_TP_EVENT_NONE);

    frame = make_dt(UINT8_C(0x91),
                    DEVICE_SA,
                    1U,
                    input,
                    0U,
                    sizeof(input));
    assert(!boot_j1939_tp_rx_handle(&receiver,
                                    &frame,
                                    2U,
                                    &response,
                                    &ready,
                                    &event));
    assert(!ready);
    assert(event == BOOT_J1939_TP_EVENT_NONE);

    frame = make_cm(UINT8_C(0x91),
                    BOOT_J1939_GLOBAL_ADDRESS,
                    BOOT_J1939_TP_CONTROL_BAM,
                    9U,
                    2U,
                    UINT8_C(0xFF),
                    BOOT_J1939_PGN_COMMANDED_ADDRESS);
    assert(!boot_j1939_tp_rx_handle(&receiver,
                                    &frame,
                                    3U,
                                    &response,
                                    &ready,
                                    &event));
    assert(!ready);
    assert(event == BOOT_J1939_TP_EVENT_NONE);

    frame = make_dt(TOOL_SA,
                    DEVICE_SA,
                    1U,
                    input,
                    0U,
                    sizeof(input));
    assert(boot_j1939_tp_rx_handle(&receiver,
                                   &frame,
                                   4U,
                                   &response,
                                   &ready,
                                   &event));
    assert(!ready);
    assert(event == BOOT_J1939_TP_EVENT_NONE);
    assert(receiver.state == BOOT_J1939_TP_RX_RTS_CTS);
    assert(receiver.bytes_received == 7U);
    assert(receiver.next_sequence == 2U);
}

static void test_expected_rts_has_priority_over_bam(void)
{
    boot_j1939_tp_rx_t receiver;
    boot_can_frame_t frame;
    boot_can_frame_t response;
    boot_j1939_tp_event_t event;
    bool ready;
    uint8_t dm16_buffer[101];
    uint8_t commanded_address_buffer[9];

    boot_j1939_tp_rx_init(&receiver);
    assert(boot_j1939_tp_rx_allow_bam(&receiver,
                                      BOOT_J1939_PGN_COMMANDED_ADDRESS,
                                      commanded_address_buffer,
                                      sizeof(commanded_address_buffer)));
    assert(boot_j1939_tp_rx_expect_rts(&receiver,
                                       TOOL_SA,
                                       DEVICE_SA,
                                       BOOT_J1939_PGN_DM16,
                                       sizeof(dm16_buffer),
                                       dm16_buffer,
                                       sizeof(dm16_buffer)));

    frame = make_cm(UINT8_C(0x91),
                    BOOT_J1939_GLOBAL_ADDRESS,
                    BOOT_J1939_TP_CONTROL_BAM,
                    sizeof(commanded_address_buffer),
                    2U,
                    UINT8_C(0xFF),
                    BOOT_J1939_PGN_COMMANDED_ADDRESS);
    assert(!boot_j1939_tp_rx_handle(&receiver,
                                    &frame,
                                    0U,
                                    &response,
                                    &ready,
                                    &event));
    assert(!ready);
    assert(event == BOOT_J1939_TP_EVENT_NONE);
    assert(receiver.state == BOOT_J1939_TP_RX_IDLE);
    assert(receiver.expected_rts_valid);

    frame = make_cm(TOOL_SA,
                    DEVICE_SA,
                    BOOT_J1939_TP_CONTROL_RTS,
                    sizeof(dm16_buffer),
                    15U,
                    8U,
                    BOOT_J1939_PGN_DM16);
    assert(boot_j1939_tp_rx_handle(&receiver,
                                   &frame,
                                   1U,
                                   &response,
                                   &ready,
                                   &event));
    assert(ready);
    assert(event == BOOT_J1939_TP_EVENT_STARTED);
}

static void test_commanded_address_bam(void)
{
    boot_j1939_tp_rx_t receiver;
    boot_can_frame_t frame;
    boot_can_frame_t response;
    boot_j1939_tp_event_t event;
    bool ready;
    uint8_t input[9];
    uint8_t output[9] = {0U};

    boot_protocol_write_u64_le(input, UINT64_C(0x8000000000000100));
    input[8] = UINT8_C(0x82);
    boot_j1939_tp_rx_init(&receiver);
    assert(boot_j1939_tp_rx_allow_bam(&receiver,
                                      BOOT_J1939_PGN_COMMANDED_ADDRESS,
                                      output,
                                      sizeof(output)));
    frame = make_cm(TOOL_SA,
                    BOOT_J1939_GLOBAL_ADDRESS,
                    BOOT_J1939_TP_CONTROL_BAM,
                    sizeof(input),
                    2U,
                    UINT8_C(0xFF),
                    BOOT_J1939_PGN_COMMANDED_ADDRESS);
    assert(boot_j1939_tp_rx_handle(&receiver,
                                   &frame,
                                   0U,
                                   &response,
                                   &ready,
                                   &event));
    assert(!ready);
    assert(event == BOOT_J1939_TP_EVENT_STARTED);

    frame = make_dt(TOOL_SA,
                    BOOT_J1939_GLOBAL_ADDRESS,
                    1U,
                    input,
                    0U,
                    sizeof(input));
    assert(boot_j1939_tp_rx_handle(&receiver,
                                   &frame,
                                   1U,
                                   &response,
                                   &ready,
                                   &event));
    assert(!ready);
    frame = make_dt(TOOL_SA,
                    BOOT_J1939_GLOBAL_ADDRESS,
                    2U,
                    input,
                    7U,
                    sizeof(input));
    assert(boot_j1939_tp_rx_handle(&receiver,
                                   &frame,
                                   2U,
                                   &response,
                                   &ready,
                                   &event));
    assert(!ready);
    assert(event == BOOT_J1939_TP_EVENT_COMPLETE);
    assert(boot_j1939_tp_rx_pgn(&receiver) ==
           BOOT_J1939_PGN_COMMANDED_ADDRESS);
    assert(memcmp(output, input, sizeof(input)) == 0);
}

int main(void)
{
    test_rts_cts_complete_two_windows();
    test_sequence_error_and_timeout();
    test_rejects_unexpected_rts();
    test_ignores_foreign_transfer_while_active();
    test_expected_rts_has_priority_over_bam();
    test_commanded_address_bam();

    puts("test_j1939_tp: OK");
    return 0;
}
