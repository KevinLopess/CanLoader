#include "boot_j1939_tp.h"

#include <stddef.h>

#include "boot_config.h"
#include "boot_j1939.h"
#include "boot_protocol.h"

static bool deadline_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static uint8_t packet_count_for_size(uint16_t message_size)
{
    return (uint8_t)((message_size + 6U) / 7U);
}

static uint8_t minimum_u8(uint8_t first, uint8_t second)
{
    return (first < second) ? first : second;
}

static bool make_cm(boot_can_frame_t *response,
                    uint8_t source_address,
                    uint8_t destination_address,
                    const uint8_t *payload)
{
    return boot_j1939_make_frame(response,
                                 BOOT_J1939_PRIORITY,
                                 BOOT_J1939_PGN_TP_CM,
                                 source_address,
                                 destination_address,
                                 payload,
                                 BOOT_CAN_MAX_DATA_LENGTH);
}

static bool make_cts(const boot_j1939_tp_rx_t *receiver,
                     boot_can_frame_t *response,
                     uint8_t packet_count,
                     uint8_t next_sequence)
{
    uint8_t payload[BOOT_CAN_MAX_DATA_LENGTH] = {
        BOOT_J1939_TP_CONTROL_CTS,
        packet_count,
        next_sequence,
        UINT8_C(0xFF),
        UINT8_C(0xFF),
        0U,
        0U,
        0U
    };

    boot_protocol_write_u24_le(&payload[5], receiver->pgn);
    return make_cm(response,
                   receiver->destination_address,
                   receiver->source_address,
                   payload);
}

static bool make_eom_ack(const boot_j1939_tp_rx_t *receiver,
                         boot_can_frame_t *response)
{
    uint8_t payload[BOOT_CAN_MAX_DATA_LENGTH] = {
        BOOT_J1939_TP_CONTROL_EOM_ACK,
        0U,
        0U,
        0U,
        UINT8_C(0xFF),
        0U,
        0U,
        0U
    };

    boot_protocol_write_u16_le(&payload[1], receiver->message_size);
    payload[3] = receiver->total_packets;
    boot_protocol_write_u24_le(&payload[5], receiver->pgn);
    return make_cm(response,
                   receiver->destination_address,
                   receiver->source_address,
                   payload);
}

static bool make_abort(uint8_t source_address,
                       uint8_t destination_address,
                       uint32_t pgn,
                       uint8_t reason,
                       boot_can_frame_t *response)
{
    uint8_t payload[BOOT_CAN_MAX_DATA_LENGTH] = {
        BOOT_J1939_TP_CONTROL_ABORT,
        reason,
        UINT8_C(0xFF),
        UINT8_C(0xFF),
        UINT8_C(0xFF),
        0U,
        0U,
        0U
    };

    boot_protocol_write_u24_le(&payload[5], pgn);
    return make_cm(response,
                   source_address,
                   destination_address,
                   payload);
}

static void clear_active(boot_j1939_tp_rx_t *receiver)
{
    receiver->state = BOOT_J1939_TP_RX_IDLE;
    receiver->deadline_ms = 0U;
    receiver->packets_until_cts = 0U;
    receiver->sender_max_packets = 0U;
}

void boot_j1939_tp_rx_init(boot_j1939_tp_rx_t *receiver)
{
    if (receiver != NULL)
    {
        *receiver = (boot_j1939_tp_rx_t){0};
    }
}

bool boot_j1939_tp_rx_expect_rts(boot_j1939_tp_rx_t *receiver,
                                 uint8_t source_address,
                                 uint8_t destination_address,
                                 uint32_t pgn,
                                 uint16_t message_size,
                                 uint8_t *buffer,
                                 uint16_t capacity)
{
    if ((receiver == NULL) || (buffer == NULL) ||
        (receiver->state != BOOT_J1939_TP_RX_IDLE) ||
        (source_address >= BOOT_J1939_NULL_ADDRESS) ||
        (destination_address >= BOOT_J1939_NULL_ADDRESS) ||
        (pgn > BOOT_J1939_MAX_PGN) ||
        (message_size <= BOOT_CAN_MAX_DATA_LENGTH) ||
        (message_size > BOOT_J1939_TP_MAX_MESSAGE_SIZE) ||
        (capacity < message_size))
    {
        return false;
    }

    receiver->expected_rts_source = source_address;
    receiver->expected_rts_destination = destination_address;
    receiver->expected_rts_pgn = pgn;
    receiver->expected_rts_size = message_size;
    receiver->expected_rts_buffer = buffer;
    receiver->buffer_capacity = capacity;
    receiver->expected_rts_valid = true;
    return true;
}

bool boot_j1939_tp_rx_allow_bam(boot_j1939_tp_rx_t *receiver,
                                uint32_t pgn,
                                uint8_t *buffer,
                                uint16_t capacity)
{
    if ((receiver == NULL) || (buffer == NULL) ||
        (pgn > BOOT_J1939_MAX_PGN) ||
        (capacity <= BOOT_CAN_MAX_DATA_LENGTH) ||
        (capacity > BOOT_J1939_TP_MAX_MESSAGE_SIZE))
    {
        return false;
    }

    receiver->allowed_bam_pgn = pgn;
    receiver->bam_buffer = buffer;
    receiver->bam_capacity = capacity;
    receiver->bam_allowed = true;
    return true;
}

static bool handle_rts(boot_j1939_tp_rx_t *receiver,
                       const boot_j1939_header_t *header,
                       const boot_can_frame_t *frame,
                       uint32_t now_ms,
                       boot_can_frame_t *response,
                       bool *response_ready,
                       boot_j1939_tp_event_t *event)
{
    const uint16_t message_size = boot_protocol_read_u16_le(&frame->data[1]);
    const uint8_t total_packets = frame->data[3];
    const uint8_t sender_max_packets = frame->data[4];
    const uint32_t pgn = boot_protocol_read_u24_le(&frame->data[5]);
    uint8_t window;

    if (receiver->state != BOOT_J1939_TP_RX_IDLE)
    {
        if (!header->destination_specific ||
            (header->destination_address != receiver->destination_address) ||
            (header->source_address != receiver->source_address) ||
            (pgn != receiver->pgn))
        {
            return false;
        }
        *response_ready = make_abort(header->destination_address,
                                     header->source_address,
                                     pgn,
                                     BOOT_J1939_TP_ABORT_BUSY,
                                     response);
        clear_active(receiver);
        *event = BOOT_J1939_TP_EVENT_REJECTED;
        return true;
    }

    if (!receiver->expected_rts_valid)
    {
        return false;
    }
    if (!header->destination_specific ||
        (header->destination_address != receiver->expected_rts_destination))
    {
        return false;
    }
    if ((header->source_address != receiver->expected_rts_source) ||
        (pgn != receiver->expected_rts_pgn))
    {
        return false;
    }
    if ((message_size != receiver->expected_rts_size) ||
        (message_size <= BOOT_CAN_MAX_DATA_LENGTH) ||
        (message_size > BOOT_J1939_TP_MAX_MESSAGE_SIZE) ||
        (message_size > receiver->buffer_capacity) ||
        (total_packets != packet_count_for_size(message_size)) ||
        (sender_max_packets == 0U))
    {
        /* Keep the rejected transfer identity available to the integration
         * layer so it can isolate failures on a multi-ECU bus. */
        receiver->pgn = pgn;
        receiver->source_address = header->source_address;
        receiver->destination_address = header->destination_address;
        *response_ready = make_abort(header->destination_address,
                                     header->source_address,
                                     pgn,
                                     BOOT_J1939_TP_ABORT_RESOURCES,
                                     response);
        receiver->expected_rts_valid = false;
        *event = BOOT_J1939_TP_EVENT_REJECTED;
        return true;
    }

    receiver->state = BOOT_J1939_TP_RX_RTS_CTS;
    receiver->pgn = pgn;
    receiver->message_size = message_size;
    receiver->bytes_received = 0U;
    receiver->source_address = header->source_address;
    receiver->destination_address = header->destination_address;
    receiver->total_packets = total_packets;
    receiver->next_sequence = 1U;
    receiver->sender_max_packets = sender_max_packets;
    receiver->buffer = receiver->expected_rts_buffer;
    receiver->expected_rts_valid = false;
    receiver->deadline_ms = now_ms + BOOT_J1939_TP_TIMEOUT_MS;

    window = minimum_u8(BOOT_J1939_TP_CTS_WINDOW, sender_max_packets);
    window = minimum_u8(window, total_packets);
    receiver->packets_until_cts = window;
    *response_ready = make_cts(receiver, response, window, 1U);
    *event = BOOT_J1939_TP_EVENT_STARTED;
    return true;
}

static bool handle_bam(boot_j1939_tp_rx_t *receiver,
                       const boot_j1939_header_t *header,
                       const boot_can_frame_t *frame,
                       uint32_t now_ms,
                       boot_j1939_tp_event_t *event)
{
    const uint16_t message_size = boot_protocol_read_u16_le(&frame->data[1]);
    const uint8_t total_packets = frame->data[3];
    const uint32_t pgn = boot_protocol_read_u24_le(&frame->data[5]);

    if (!receiver->bam_allowed || !header->destination_specific ||
        (header->destination_address != BOOT_J1939_GLOBAL_ADDRESS) ||
        (pgn != receiver->allowed_bam_pgn))
    {
        return false;
    }
    if ((receiver->state != BOOT_J1939_TP_RX_IDLE) ||
        receiver->expected_rts_valid)
    {
        return false;
    }
    if ((message_size <= BOOT_CAN_MAX_DATA_LENGTH) ||
        (message_size > receiver->bam_capacity) ||
        (message_size > BOOT_J1939_TP_MAX_MESSAGE_SIZE) ||
        (total_packets != packet_count_for_size(message_size)) ||
        (frame->data[4] != UINT8_C(0xFF)))
    {
        return false;
    }

    receiver->state = BOOT_J1939_TP_RX_BAM;
    receiver->pgn = pgn;
    receiver->message_size = message_size;
    receiver->bytes_received = 0U;
    receiver->source_address = header->source_address;
    receiver->destination_address = BOOT_J1939_GLOBAL_ADDRESS;
    receiver->total_packets = total_packets;
    receiver->next_sequence = 1U;
    receiver->buffer = receiver->bam_buffer;
    receiver->deadline_ms = now_ms + BOOT_J1939_TP_TIMEOUT_MS;
    *event = BOOT_J1939_TP_EVENT_STARTED;
    return true;
}

static bool handle_cm(boot_j1939_tp_rx_t *receiver,
                      const boot_j1939_header_t *header,
                      const boot_can_frame_t *frame,
                      uint32_t now_ms,
                      boot_can_frame_t *response,
                      bool *response_ready,
                      boot_j1939_tp_event_t *event)
{
    const uint8_t control = frame->data[0];

    if (frame->dlc != BOOT_CAN_MAX_DATA_LENGTH)
    {
        return false;
    }

    if (control == BOOT_J1939_TP_CONTROL_RTS)
    {
        return handle_rts(receiver,
                          header,
                          frame,
                          now_ms,
                          response,
                          response_ready,
                          event);
    }
    if (control == BOOT_J1939_TP_CONTROL_BAM)
    {
        return handle_bam(receiver, header, frame, now_ms, event);
    }
    if (control == BOOT_J1939_TP_CONTROL_ABORT)
    {
        const uint32_t pgn = boot_protocol_read_u24_le(&frame->data[5]);

        if ((receiver->state == BOOT_J1939_TP_RX_RTS_CTS) &&
            (header->source_address == receiver->source_address) &&
            (header->destination_address == receiver->destination_address) &&
            (pgn == receiver->pgn))
        {
            clear_active(receiver);
            *event = BOOT_J1939_TP_EVENT_ABORTED;
            return true;
        }
        return false;
    }

    return false;
}

static bool handle_dt(boot_j1939_tp_rx_t *receiver,
                      const boot_j1939_header_t *header,
                      const boot_can_frame_t *frame,
                      uint32_t now_ms,
                      boot_can_frame_t *response,
                      bool *response_ready,
                      boot_j1939_tp_event_t *event)
{
    uint16_t remaining;
    uint8_t copy_length;
    uint8_t index;

    if (receiver->state == BOOT_J1939_TP_RX_IDLE)
    {
        return false;
    }
    if ((frame->dlc != BOOT_CAN_MAX_DATA_LENGTH) ||
        (header->source_address != receiver->source_address) ||
        (header->destination_address != receiver->destination_address))
    {
        return false;
    }

    if (frame->data[0] != receiver->next_sequence)
    {
        if (receiver->state == BOOT_J1939_TP_RX_RTS_CTS)
        {
            *response_ready = make_abort(receiver->destination_address,
                                         receiver->source_address,
                                         receiver->pgn,
                                         BOOT_J1939_TP_ABORT_RESOURCES,
                                         response);
        }
        clear_active(receiver);
        *event = BOOT_J1939_TP_EVENT_ABORTED;
        return true;
    }

    remaining = (uint16_t)(receiver->message_size - receiver->bytes_received);
    copy_length = (remaining > 7U) ? 7U : (uint8_t)remaining;
    for (index = 0U; index < copy_length; ++index)
    {
        receiver->buffer[receiver->bytes_received + index] = frame->data[index + 1U];
    }
    receiver->bytes_received = (uint16_t)(receiver->bytes_received + copy_length);
    ++receiver->next_sequence;
    receiver->deadline_ms = now_ms + BOOT_J1939_TP_TIMEOUT_MS;

    if (receiver->bytes_received == receiver->message_size)
    {
        if ((uint8_t)(receiver->next_sequence - 1U) != receiver->total_packets)
        {
            clear_active(receiver);
            *event = BOOT_J1939_TP_EVENT_ABORTED;
            return true;
        }
        if (receiver->state == BOOT_J1939_TP_RX_RTS_CTS)
        {
            *response_ready = make_eom_ack(receiver, response);
        }
        clear_active(receiver);
        *event = BOOT_J1939_TP_EVENT_COMPLETE;
        return true;
    }

    if (receiver->state == BOOT_J1939_TP_RX_RTS_CTS)
    {
        uint8_t packets_remaining;
        uint8_t window;

        --receiver->packets_until_cts;
        if (receiver->packets_until_cts == 0U)
        {
            packets_remaining =
                (uint8_t)(receiver->total_packets - (receiver->next_sequence - 1U));
            window = minimum_u8(BOOT_J1939_TP_CTS_WINDOW,
                                receiver->sender_max_packets);
            window = minimum_u8(window, packets_remaining);
            receiver->packets_until_cts = window;
            *response_ready = make_cts(receiver,
                                       response,
                                       window,
                                       receiver->next_sequence);
        }
    }
    return true;
}

bool boot_j1939_tp_rx_handle(boot_j1939_tp_rx_t *receiver,
                             const boot_can_frame_t *frame,
                             uint32_t now_ms,
                             boot_can_frame_t *response,
                             bool *response_ready,
                             boot_j1939_tp_event_t *event)
{
    boot_j1939_header_t header;

    if ((receiver == NULL) || (frame == NULL) || (response == NULL) ||
        (response_ready == NULL) || (event == NULL))
    {
        return false;
    }
    *response_ready = false;
    *event = BOOT_J1939_TP_EVENT_NONE;

    if (!boot_j1939_decode_frame(frame, &header))
    {
        return false;
    }
    if (header.source_address >= BOOT_J1939_NULL_ADDRESS)
    {
        return false;
    }
    if (header.pgn == BOOT_J1939_PGN_TP_CM)
    {
        return handle_cm(receiver,
                         &header,
                         frame,
                         now_ms,
                         response,
                         response_ready,
                         event);
    }
    if (header.pgn == BOOT_J1939_PGN_TP_DT)
    {
        return handle_dt(receiver,
                         &header,
                         frame,
                         now_ms,
                         response,
                         response_ready,
                         event);
    }
    return false;
}

void boot_j1939_tp_rx_task(boot_j1939_tp_rx_t *receiver,
                           uint32_t now_ms,
                           boot_can_frame_t *response,
                           bool *response_ready,
                           boot_j1939_tp_event_t *event)
{
    if ((response_ready == NULL) || (event == NULL))
    {
        return;
    }
    *response_ready = false;
    *event = BOOT_J1939_TP_EVENT_NONE;

    if ((receiver == NULL) || (response == NULL) ||
        (receiver->state == BOOT_J1939_TP_RX_IDLE) ||
        !deadline_reached(now_ms, receiver->deadline_ms))
    {
        return;
    }

    if (receiver->state == BOOT_J1939_TP_RX_RTS_CTS)
    {
        *response_ready = make_abort(receiver->destination_address,
                                     receiver->source_address,
                                     receiver->pgn,
                                     BOOT_J1939_TP_ABORT_TIMEOUT,
                                     response);
    }
    clear_active(receiver);
    *event = BOOT_J1939_TP_EVENT_TIMEOUT;
}

void boot_j1939_tp_rx_cancel(boot_j1939_tp_rx_t *receiver)
{
    if (receiver != NULL)
    {
        clear_active(receiver);
        receiver->expected_rts_valid = false;
    }
}

const uint8_t *boot_j1939_tp_rx_data(const boot_j1939_tp_rx_t *receiver)
{
    return (receiver == NULL) ? NULL : receiver->buffer;
}

uint16_t boot_j1939_tp_rx_size(const boot_j1939_tp_rx_t *receiver)
{
    return (receiver == NULL) ? 0U : receiver->message_size;
}

uint32_t boot_j1939_tp_rx_pgn(const boot_j1939_tp_rx_t *receiver)
{
    return (receiver == NULL) ? 0U : receiver->pgn;
}

uint8_t boot_j1939_tp_rx_source(const boot_j1939_tp_rx_t *receiver)
{
    return (receiver == NULL) ? BOOT_J1939_NULL_ADDRESS : receiver->source_address;
}
