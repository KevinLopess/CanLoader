#include "bootloader.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boot_can.h"
#include "boot_config.h"
#include "boot_device_config.h"
#include "boot_flash.h"
#include "boot_image.h"
#include "boot_j1939.h"
#include "boot_j1939_address_claim.h"
#include "boot_j1939_tp.h"
#include "boot_metadata.h"
#include "boot_platform.h"
#include "boot_protocol.h"

typedef enum
{
    BOOT_DM_OPERATION_NONE = 0,
    BOOT_DM_OPERATION_ERASE,
    BOOT_DM_OPERATION_WRITE
} boot_dm_operation_t;

typedef struct
{
    bootloader_state_t state;
    uint32_t state_started_ms;
    uint32_t last_activity_ms;
    uint32_t last_led_ms;
    uint32_t expected_size;
    uint32_t expected_crc32;
    uint32_t bytes_committed;
    uint32_t operation_pointer;
    uint32_t last_operation_pointer;
    uint16_t operation_length;
    uint16_t last_operation_length;
    uint8_t session;
    uint8_t owner_address;
    uint8_t can_filter_address;
    uint8_t tx_head;
    uint8_t tx_count;
    boot_dm_operation_t operation;
    boot_dm_operation_t last_operation;
    bool app_valid;
    bool boot_requested;
    bool crc_received;
    bool erase_pending;
    bool flash_was_erased;
    bool dm16_expected;
    bool close_pending;
    bool last_operation_valid;
    bool led_on;
    boot_device_config_t device_config;
    boot_j1939_address_claim_t address_claim;
    boot_j1939_tp_rx_t tp_receiver;
    boot_can_frame_t tx_queue[BOOT_TX_QUEUE_CAPACITY];
    uint8_t tp_buffer[BOOT_TRANSFER_BLOCK_SIZE + 1U];
    uint8_t block_buffer[BOOT_TRANSFER_BLOCK_SIZE + BOOT_FLASH_PROGRAM_UNIT - 1U];
    uint8_t commanded_address_buffer[9];
} bootloader_context_t;

static bootloader_context_t context;

static uint32_t elapsed_ms(uint32_t now, uint32_t then)
{
    return now - then;
}

static void set_state(bootloader_state_t state, uint32_t now)
{
    context.state = state;
    context.state_started_ms = now;
}

static uint8_t local_address(void)
{
    return boot_j1939_address_claim_source_address(&context.address_claim);
}

static bool tx_queue_has_space(void)
{
    return context.tx_count < BOOT_TX_QUEUE_CAPACITY;
}

static bool queue_frame(const boot_can_frame_t *frame)
{
    uint8_t tail;

    if ((frame == NULL) || !tx_queue_has_space())
    {
        return false;
    }

    tail = (uint8_t)((context.tx_head + context.tx_count) %
                     BOOT_TX_QUEUE_CAPACITY);
    context.tx_queue[tail] = *frame;
    ++context.tx_count;
    return true;
}

static void flush_tx_queue(void)
{
    while (context.tx_count > 0U)
    {
        const boot_can_status_t status =
            boot_can_send(&context.tx_queue[context.tx_head]);

        if (status != BOOT_CAN_STATUS_OK)
        {
            break;
        }
        context.tx_head = (uint8_t)((context.tx_head + 1U) %
                                    BOOT_TX_QUEUE_CAPACITY);
        --context.tx_count;
    }
}

static void collect_claim_tx(void)
{
    boot_can_frame_t frame;

    while (tx_queue_has_space() &&
           boot_j1939_address_claim_take_tx(&context.address_claim, &frame))
    {
        (void)queue_frame(&frame);
    }
}

static bool queue_control_response(uint8_t destination_address,
                                   boot_response_kind_t kind,
                                   boot_command_t command,
                                   uint8_t session,
                                   uint32_t value)
{
    boot_can_frame_t frame;

    if (!boot_protocol_make_response(&frame,
                                     local_address(),
                                     destination_address,
                                     kind,
                                     command,
                                     session,
                                     value))
    {
        return false;
    }
    return queue_frame(&frame);
}

static bool queue_ack(const boot_protocol_request_t *request, uint32_t value)
{
    return queue_control_response(request->source_address,
                                  BOOT_RESPONSE_ACK,
                                  request->command,
                                  request->session,
                                  value);
}

static bool queue_nack(const boot_protocol_request_t *request, boot_error_t error)
{
    return queue_control_response(request->source_address,
                                  BOOT_RESPONSE_NACK,
                                  request->command,
                                  request->session,
                                  (uint32_t)error);
}

static bool queue_dm15(uint8_t destination_address,
                       boot_dm15_status_t status,
                       uint16_t length,
                       boot_error_t error)
{
    boot_can_frame_t frame;

    if (!boot_protocol_make_dm15(&frame,
                                 local_address(),
                                 destination_address,
                                 status,
                                 length,
                                 (uint32_t)error,
                                 0U,
                                 UINT16_MAX))
    {
        return false;
    }
    return queue_frame(&frame);
}

static void reset_tp_expectation(void)
{
    boot_j1939_tp_rx_cancel(&context.tp_receiver);
    (void)boot_j1939_tp_rx_allow_bam(&context.tp_receiver,
                                     BOOT_J1939_PGN_COMMANDED_ADDRESS,
                                     context.commanded_address_buffer,
                                     sizeof(context.commanded_address_buffer));
}

static void clear_update(void)
{
    context.expected_size = 0U;
    context.expected_crc32 = 0U;
    context.bytes_committed = 0U;
    context.operation_pointer = 0U;
    context.last_operation_pointer = 0U;
    context.operation_length = 0U;
    context.last_operation_length = 0U;
    context.session = 0U;
    context.owner_address = BOOT_J1939_NULL_ADDRESS;
    context.operation = BOOT_DM_OPERATION_NONE;
    context.last_operation = BOOT_DM_OPERATION_NONE;
    context.crc_received = false;
    context.erase_pending = false;
    context.flash_was_erased = false;
    context.dm16_expected = false;
    context.close_pending = false;
    context.last_operation_valid = false;
    reset_tp_expectation();
}

static bool update_state_active(void)
{
    return (context.state == BOOTLOADER_STATE_PREPARE_UPDATE) ||
           (context.state == BOOTLOADER_STATE_ERASE) ||
           (context.state == BOOTLOADER_STATE_RECEIVE) ||
           (context.state == BOOTLOADER_STATE_VERIFY);
}

static bool owner_matches(uint8_t source_address, uint8_t session)
{
    return update_state_active() && (session != 0U) &&
           (session == context.session) &&
           (source_address == context.owner_address);
}

static void invalidate_update(uint32_t now)
{
    if (context.flash_was_erased)
    {
        (void)boot_metadata_mark_invalid();
        context.app_valid = false;
    }
    clear_update();
    set_state(BOOTLOADER_STATE_WAIT_HOST, now);
}

static bool apply_claim_address_change(uint32_t now)
{
    uint8_t new_address;

    if (!boot_j1939_address_claim_take_address_changed(&context.address_claim,
                                                        &new_address))
    {
        return true;
    }
    if (new_address == context.can_filter_address)
    {
        return true;
    }

    /* Never transmit a queued response carrying the previous source address
     * after an address transition. */
    context.tx_head = 0U;
    context.tx_count = 0U;
    if (!boot_can_abort_pending_tx())
    {
        set_state(BOOTLOADER_STATE_ERROR, now);
        return false;
    }
    if (update_state_active())
    {
        invalidate_update(now);
    }
    if (!boot_can_configure_address(new_address))
    {
        set_state(BOOTLOADER_STATE_ERROR, now);
        return false;
    }
    context.can_filter_address = new_address;
    return true;
}

static uint32_t capability_word(void)
{
    uint32_t flags = BOOT_CAPABILITY_J1939 |
                     BOOT_CAPABILITY_ADDRESS_CLAIM |
                     BOOT_CAPABILITY_TP_RX;

    if (context.app_valid)
    {
        flags |= BOOT_CAPABILITY_APPLICATION_VALID;
    }
    if (update_state_active())
    {
        flags |= BOOT_CAPABILITY_UPDATE_ACTIVE;
    }

    return flags |
           ((uint32_t)local_address() << 8U) |
           ((uint32_t)BOOT_PROTOCOL_VERSION << 16U) |
           ((uint32_t)BOOT_TARGET_CODE << 24U);
}

static bool command_known(boot_command_t command)
{
    switch (command)
    {
        case BOOT_COMMAND_IDENTIFY:
        case BOOT_COMMAND_INFO:
        case BOOT_COMMAND_START_UPDATE:
        case BOOT_COMMAND_SET_IMAGE_CRC:
        case BOOT_COMMAND_FINISH:
        case BOOT_COMMAND_VERIFY:
        case BOOT_COMMAND_RUN_APPLICATION:
        case BOOT_COMMAND_ENTER_BOOTLOADER:
        case BOOT_COMMAND_ABORT:
            return true;

        default:
            return false;
    }
}

static void handle_identify(const boot_protocol_request_t *request)
{
    if ((request->parameter != 0U) || (request->value != 0U))
    {
        (void)queue_nack(request, BOOT_ERROR_MALFORMED_FRAME);
        return;
    }
    (void)queue_ack(request, capability_word());
}

static void handle_info(const boot_protocol_request_t *request)
{
    boot_metadata_t metadata;
    uint32_t value;

    if (request->value != 0U)
    {
        (void)queue_nack(request, BOOT_ERROR_MALFORMED_FRAME);
        return;
    }

    switch ((boot_info_selector_t)request->parameter)
    {
        case BOOT_INFO_CAPABILITIES:
            value = capability_word();
            break;
        case BOOT_INFO_APPLICATION_BASE:
            value = BOOT_APPLICATION_ADDRESS;
            break;
        case BOOT_INFO_APPLICATION_MAX_SIZE:
            value = BOOT_APPLICATION_MAX_SIZE;
            break;
        case BOOT_INFO_STORED_IMAGE_SIZE:
            value = boot_metadata_read(&metadata) ? metadata.image_size : 0U;
            break;
        case BOOT_INFO_STORED_IMAGE_CRC:
            value = boot_metadata_read(&metadata) ? metadata.image_crc32 : 0U;
            break;
        case BOOT_INFO_UID_WORD_0:
            value = boot_platform_uid_word(0U);
            break;
        case BOOT_INFO_UID_WORD_1:
            value = boot_platform_uid_word(1U);
            break;
        case BOOT_INFO_UID_WORD_2:
            value = boot_platform_uid_word(2U);
            break;
        case BOOT_INFO_NAME_LOW:
            value = (uint32_t)context.device_config.name;
            break;
        case BOOT_INFO_NAME_HIGH:
            value = (uint32_t)(context.device_config.name >> 32U);
            break;
        case BOOT_INFO_SOURCE_ADDRESS:
            value = local_address();
            break;
        case BOOT_INFO_FLASH_ERASE_UNIT_SIZE:
            value = BOOT_FLASH_PAGE_SIZE;
            break;
        default:
            (void)queue_nack(request, BOOT_ERROR_INVALID_COMMAND);
            return;
    }
    (void)queue_ack(request, value);
}

static void handle_start_update(const boot_protocol_request_t *request,
                                uint32_t now)
{
    if ((request->session == 0U) || (request->parameter != 0U))
    {
        (void)queue_nack(request, BOOT_ERROR_SESSION);
        return;
    }

    if (update_state_active())
    {
        if ((request->source_address == context.owner_address) &&
            (request->session == context.session) &&
            (request->value == context.expected_size))
        {
            context.last_activity_ms = now;
            (void)queue_ack(request, BOOT_TRANSFER_BLOCK_SIZE);
        }
        else
        {
            (void)queue_nack(request, BOOT_ERROR_BUSY);
        }
        return;
    }

    if ((context.state != BOOTLOADER_STATE_BOOT_WINDOW) &&
        (context.state != BOOTLOADER_STATE_WAIT_HOST) &&
        (context.state != BOOTLOADER_STATE_READY))
    {
        (void)queue_nack(request, BOOT_ERROR_INVALID_STATE);
        return;
    }
    if ((request->value < 8U) || (request->value > BOOT_APPLICATION_MAX_SIZE))
    {
        (void)queue_nack(request, BOOT_ERROR_INVALID_SIZE);
        return;
    }

    clear_update();
    context.owner_address = request->source_address;
    context.session = request->session;
    context.expected_size = request->value;
    context.last_activity_ms = now;
    set_state(BOOTLOADER_STATE_PREPARE_UPDATE, now);
    (void)queue_ack(request, BOOT_TRANSFER_BLOCK_SIZE);
}

static void handle_set_crc(const boot_protocol_request_t *request, uint32_t now)
{
    if (!owner_matches(request->source_address, request->session))
    {
        (void)queue_nack(request, BOOT_ERROR_SESSION);
        return;
    }
    if ((context.state != BOOTLOADER_STATE_PREPARE_UPDATE) ||
        (request->parameter != 0U))
    {
        (void)queue_nack(request, BOOT_ERROR_INVALID_STATE);
        return;
    }
    if (context.crc_received && (context.expected_crc32 != request->value))
    {
        (void)queue_nack(request, BOOT_ERROR_INVALID_STATE);
        return;
    }

    context.expected_crc32 = request->value;
    context.crc_received = true;
    context.last_activity_ms = now;
    (void)queue_ack(request, request->value);
}

static void fail_finish(const boot_protocol_request_t *request,
                        boot_error_t error,
                        uint32_t now)
{
    const uint8_t destination = request->source_address;
    const uint8_t session = request->session;

    invalidate_update(now);
    (void)queue_control_response(destination,
                                 BOOT_RESPONSE_NACK,
                                 BOOT_COMMAND_FINISH,
                                 session,
                                 (uint32_t)error);
}

static void handle_finish(const boot_protocol_request_t *request, uint32_t now)
{
    uint32_t computed_crc;
    uint32_t stack_pointer;
    uint32_t reset_handler;
    boot_metadata_t metadata;

    if ((request->parameter != 0U) ||
        (request->value != context.expected_crc32))
    {
        (void)queue_nack(request, BOOT_ERROR_MALFORMED_FRAME);
        return;
    }
    if ((context.state == BOOTLOADER_STATE_READY) && context.app_valid &&
        (request->source_address == context.owner_address) &&
        (request->session == context.session) && boot_metadata_read(&metadata))
    {
        (void)queue_ack(request, metadata.image_crc32);
        return;
    }
    if (!owner_matches(request->source_address, request->session))
    {
        (void)queue_nack(request, BOOT_ERROR_SESSION);
        return;
    }
    if ((context.state != BOOTLOADER_STATE_RECEIVE) ||
        (context.operation != BOOT_DM_OPERATION_NONE) ||
        context.close_pending ||
        (context.bytes_committed != context.expected_size))
    {
        (void)queue_nack(request, BOOT_ERROR_INVALID_STATE);
        return;
    }

    set_state(BOOTLOADER_STATE_VERIFY, now);
    if (!boot_image_calculate_crc(context.expected_size, &computed_crc) ||
        (computed_crc != context.expected_crc32))
    {
        fail_finish(request, BOOT_ERROR_CRC_MISMATCH, now);
        return;
    }
    if (!boot_image_vectors_valid(context.expected_size,
                                  &stack_pointer,
                                  &reset_handler))
    {
        fail_finish(request, BOOT_ERROR_INVALID_APPLICATION, now);
        return;
    }
    if (boot_metadata_mark_valid() != BOOT_FLASH_STATUS_OK)
    {
        fail_finish(request, BOOT_ERROR_FLASH_WRITE, now);
        return;
    }
    if (!boot_image_validate(&metadata))
    {
        fail_finish(request, BOOT_ERROR_INVALID_APPLICATION, now);
        return;
    }

    context.app_valid = true;
    context.flash_was_erased = false;
    set_state(BOOTLOADER_STATE_READY, boot_platform_millis());
    (void)queue_ack(request, computed_crc);
}

static void handle_verify(const boot_protocol_request_t *request)
{
    boot_metadata_t metadata;

    if ((request->parameter != 0U) || (request->value != 0U))
    {
        (void)queue_nack(request, BOOT_ERROR_MALFORMED_FRAME);
        return;
    }
    if (update_state_active())
    {
        (void)queue_nack(request,
                         owner_matches(request->source_address,
                                       request->session) ?
                         BOOT_ERROR_BUSY : BOOT_ERROR_SESSION);
        return;
    }
    if (!boot_image_validate(&metadata))
    {
        context.app_valid = false;
        (void)queue_nack(request, BOOT_ERROR_INVALID_APPLICATION);
        return;
    }
    context.app_valid = true;
    (void)queue_ack(request, metadata.image_crc32);
}

static void handle_run(const boot_protocol_request_t *request, uint32_t now)
{
    boot_metadata_t metadata;

    if ((request->parameter != 0U) || (request->value != 0U))
    {
        (void)queue_nack(request, BOOT_ERROR_MALFORMED_FRAME);
        return;
    }
    if (update_state_active())
    {
        (void)queue_nack(request,
                         owner_matches(request->source_address,
                                       request->session) ?
                         BOOT_ERROR_BUSY : BOOT_ERROR_SESSION);
        return;
    }
    if (!boot_image_validate(&metadata))
    {
        context.app_valid = false;
        (void)queue_nack(request, BOOT_ERROR_INVALID_APPLICATION);
        return;
    }
    context.app_valid = true;
    (void)queue_ack(request, metadata.image_crc32);
    set_state(BOOTLOADER_STATE_RUN_APPLICATION, now);
}

static void handle_abort(const boot_protocol_request_t *request, uint32_t now)
{
    if ((request->parameter != 0U) || (request->value != 0U))
    {
        (void)queue_nack(request, BOOT_ERROR_MALFORMED_FRAME);
        return;
    }
    if (!update_state_active())
    {
        (void)queue_ack(request, 0U);
        return;
    }
    if (!owner_matches(request->source_address, request->session))
    {
        (void)queue_nack(request, BOOT_ERROR_SESSION);
        return;
    }

    invalidate_update(now);
    (void)queue_ack(request, 0U);
}

static void handle_control(const boot_can_frame_t *frame, uint32_t now)
{
    boot_protocol_request_t request;
    boot_error_t error;

    if (!boot_protocol_decode_request(frame, &request, &error))
    {
        boot_j1939_header_t header;

        if (boot_j1939_decode_frame(frame, &header) &&
            header.destination_specific &&
            (header.destination_address == local_address()) &&
            (frame->dlc >= 3U))
        {
            (void)queue_control_response(header.source_address,
                                         BOOT_RESPONSE_NACK,
                                         (boot_command_t)frame->data[1],
                                         frame->data[2],
                                         (uint32_t)error);
        }
        return;
    }
    if (request.destination_address != local_address())
    {
        return;
    }
    if (request.session == 0U)
    {
        (void)queue_nack(&request, BOOT_ERROR_SESSION);
        return;
    }
    if (!command_known(request.command))
    {
        (void)queue_nack(&request, BOOT_ERROR_INVALID_COMMAND);
        return;
    }

    switch (request.command)
    {
        case BOOT_COMMAND_IDENTIFY:
            handle_identify(&request);
            break;
        case BOOT_COMMAND_INFO:
            handle_info(&request);
            break;
        case BOOT_COMMAND_START_UPDATE:
            handle_start_update(&request, now);
            break;
        case BOOT_COMMAND_SET_IMAGE_CRC:
            handle_set_crc(&request, now);
            break;
        case BOOT_COMMAND_FINISH:
            handle_finish(&request, now);
            break;
        case BOOT_COMMAND_VERIFY:
            handle_verify(&request);
            break;
        case BOOT_COMMAND_RUN_APPLICATION:
            handle_run(&request, now);
            break;
        case BOOT_COMMAND_ENTER_BOOTLOADER:
            if ((request.parameter != 0U) || (request.value != 0U))
            {
                (void)queue_nack(&request, BOOT_ERROR_MALFORMED_FRAME);
            }
            else if (update_state_active())
            {
                (void)queue_nack(&request,
                                 owner_matches(request.source_address,
                                               request.session) ?
                                 BOOT_ERROR_BUSY : BOOT_ERROR_SESSION);
            }
            else
            {
                set_state(BOOTLOADER_STATE_WAIT_HOST, now);
                (void)queue_ack(&request, 0U);
            }
            break;
        case BOOT_COMMAND_ABORT:
            handle_abort(&request, now);
            break;
        default:
            (void)queue_nack(&request, BOOT_ERROR_INVALID_COMMAND);
            break;
    }
}

static bool dm_request_matches_last(const boot_dm14_request_t *request,
                                    boot_dm_operation_t operation)
{
    return context.last_operation_valid &&
           (context.last_operation == operation) &&
           (context.last_operation_pointer == request->pointer) &&
           (context.last_operation_length == request->length);
}

static bool dm_request_matches_current(const boot_dm14_request_t *request,
                                       boot_dm_operation_t operation)
{
    return (context.operation == operation) &&
           (context.operation_pointer == request->pointer) &&
           (context.operation_length == request->length);
}

static void handle_dm14_erase(const boot_dm14_request_t *request, uint32_t now)
{
    const uint32_t expected_pages =
        (context.expected_size + BOOT_FLASH_PAGE_SIZE - 1U) /
        BOOT_FLASH_PAGE_SIZE;

    if (dm_request_matches_last(request, BOOT_DM_OPERATION_ERASE))
    {
        (void)queue_dm15(request->source_address,
                         BOOT_DM15_STATUS_OPERATION_COMPLETED,
                         request->length,
                         BOOT_ERROR_NONE);
        return;
    }
    if (dm_request_matches_current(request, BOOT_DM_OPERATION_ERASE))
    {
        (void)queue_dm15(request->source_address,
                         context.erase_pending ? BOOT_DM15_STATUS_BUSY :
                                                 BOOT_DM15_STATUS_PROCEED,
                         request->length,
                         BOOT_ERROR_NONE);
        return;
    }
    if ((context.state != BOOTLOADER_STATE_PREPARE_UPDATE) ||
        !context.crc_received || context.close_pending ||
        (request->pointer != BOOT_APPLICATION_ADDRESS) ||
        (request->length != expected_pages) ||
        (request->length == 0U))
    {
        (void)queue_dm15(request->source_address,
                         BOOT_DM15_STATUS_OPERATION_FAILED,
                         request->length,
                         BOOT_ERROR_INVALID_STATE);
        return;
    }

    if (!queue_dm15(request->source_address,
                    BOOT_DM15_STATUS_PROCEED,
                    request->length,
                    BOOT_ERROR_NONE))
    {
        return;
    }
    context.operation = BOOT_DM_OPERATION_ERASE;
    context.operation_pointer = request->pointer;
    context.operation_length = request->length;
    context.erase_pending = true;
    context.last_activity_ms = now;
    set_state(BOOTLOADER_STATE_ERASE, now);
}

static uint16_t expected_write_length(void)
{
    const uint32_t remaining = context.expected_size - context.bytes_committed;

    return (uint16_t)((remaining > BOOT_TRANSFER_BLOCK_SIZE) ?
                      BOOT_TRANSFER_BLOCK_SIZE : remaining);
}

static void handle_dm14_write(const boot_dm14_request_t *request, uint32_t now)
{
    const uint32_t expected_pointer =
        BOOT_APPLICATION_ADDRESS + context.bytes_committed;
    const uint16_t expected_length = expected_write_length();

    if (dm_request_matches_last(request, BOOT_DM_OPERATION_WRITE))
    {
        (void)queue_dm15(request->source_address,
                         BOOT_DM15_STATUS_OPERATION_COMPLETED,
                         request->length,
                         BOOT_ERROR_NONE);
        return;
    }
    if (dm_request_matches_current(request, BOOT_DM_OPERATION_WRITE))
    {
        (void)queue_dm15(request->source_address,
                         BOOT_DM15_STATUS_PROCEED,
                         request->length,
                         BOOT_ERROR_NONE);
        return;
    }
    if ((context.state != BOOTLOADER_STATE_RECEIVE) ||
        context.close_pending || (request->pointer != expected_pointer) ||
        (request->length != expected_length) || (request->length == 0U))
    {
        (void)queue_dm15(request->source_address,
                         BOOT_DM15_STATUS_OPERATION_FAILED,
                         request->length,
                         (request->pointer != expected_pointer) ?
                         BOOT_ERROR_INVALID_ADDRESS : BOOT_ERROR_INVALID_SEQUENCE);
        return;
    }

    if ((request->length > 7U) &&
        !boot_j1939_tp_rx_expect_rts(&context.tp_receiver,
                                     context.owner_address,
                                     local_address(),
                                     BOOT_J1939_PGN_DM16,
                                     (uint16_t)(request->length + 1U),
                                     context.tp_buffer,
                                     sizeof(context.tp_buffer)))
    {
        (void)queue_dm15(request->source_address,
                         BOOT_DM15_STATUS_BUSY,
                         request->length,
                         BOOT_ERROR_BUSY);
        return;
    }
    if (!queue_dm15(request->source_address,
                    BOOT_DM15_STATUS_PROCEED,
                    request->length,
                    BOOT_ERROR_NONE))
    {
        boot_j1939_tp_rx_cancel(&context.tp_receiver);
        return;
    }
    context.operation = BOOT_DM_OPERATION_WRITE;
    context.operation_pointer = request->pointer;
    context.operation_length = request->length;
    context.dm16_expected = true;
    context.last_activity_ms = now;
}

static void handle_dm14_close(const boot_dm14_request_t *request, uint32_t now)
{
    if (!dm_request_matches_last(request, context.last_operation))
    {
        (void)queue_dm15(request->source_address,
                         BOOT_DM15_STATUS_OPERATION_FAILED,
                         request->length,
                         BOOT_ERROR_INVALID_SEQUENCE);
        return;
    }

    /* Operation Completed has no positive response in this profile. Treat an
     * identical retransmission as an idempotent close instead of emitting a
     * stale DM15 that could be mistaken for the following operation. */
    if (!context.close_pending)
    {
        context.last_activity_ms = now;
        return;
    }

    context.close_pending = false;
    context.operation = BOOT_DM_OPERATION_NONE;
    context.last_activity_ms = now;
}

static void handle_dm14(const boot_can_frame_t *frame, uint32_t now)
{
    boot_dm14_request_t request;
    boot_error_t error;

    if (!boot_protocol_decode_dm14(frame, &request, &error))
    {
        boot_j1939_header_t header;

        if (boot_j1939_decode_frame(frame, &header) &&
            header.destination_specific &&
            (header.destination_address == local_address()))
        {
            (void)queue_dm15(header.source_address,
                             BOOT_DM15_STATUS_OPERATION_FAILED,
                             0U,
                             error);
        }
        return;
    }
    if (request.destination_address != local_address())
    {
        return;
    }
    if (!update_state_active() ||
        (request.source_address != context.owner_address))
    {
        (void)queue_dm15(request.source_address,
                         BOOT_DM15_STATUS_OPERATION_FAILED,
                         request.length,
                         BOOT_ERROR_SESSION);
        return;
    }
    if (request.pointer_is_object || (request.key != UINT16_MAX))
    {
        (void)queue_dm15(request.source_address,
                         BOOT_DM15_STATUS_OPERATION_FAILED,
                         request.length,
                         BOOT_ERROR_UNSUPPORTED_OPERATION);
        return;
    }

    context.last_activity_ms = now;
    switch (request.command)
    {
        case BOOT_DM14_COMMAND_ERASE:
            handle_dm14_erase(&request, now);
            break;
        case BOOT_DM14_COMMAND_WRITE:
            handle_dm14_write(&request, now);
            break;
        case BOOT_DM14_COMMAND_STATUS_REQUEST:
            if (context.erase_pending)
            {
                (void)queue_dm15(request.source_address,
                                 BOOT_DM15_STATUS_BUSY,
                                 context.operation_length,
                                 BOOT_ERROR_NONE);
            }
            else if (context.close_pending)
            {
                (void)queue_dm15(request.source_address,
                                 BOOT_DM15_STATUS_OPERATION_COMPLETED,
                                 context.last_operation_length,
                                 BOOT_ERROR_NONE);
            }
            else if (context.dm16_expected)
            {
                (void)queue_dm15(request.source_address,
                                 BOOT_DM15_STATUS_PROCEED,
                                 context.operation_length,
                                 BOOT_ERROR_NONE);
            }
            else
            {
                (void)queue_dm15(request.source_address,
                                 BOOT_DM15_STATUS_OPERATION_COMPLETED,
                                 0U,
                                 BOOT_ERROR_NONE);
            }
            break;
        case BOOT_DM14_COMMAND_OPERATION_COMPLETED:
            handle_dm14_close(&request, now);
            break;
        case BOOT_DM14_COMMAND_OPERATION_FAILED:
            reset_tp_expectation();
            context.operation = BOOT_DM_OPERATION_NONE;
            context.erase_pending = false;
            context.dm16_expected = false;
            context.close_pending = false;
            context.last_activity_ms = now;
            if (context.state == BOOTLOADER_STATE_ERASE)
            {
                set_state(BOOTLOADER_STATE_PREPARE_UPDATE, now);
            }
            break;
        case BOOT_DM14_COMMAND_READ:
        case BOOT_DM14_COMMAND_BOOT_LOAD:
        case BOOT_DM14_COMMAND_EDCP_GENERATION:
        default:
            (void)queue_dm15(request.source_address,
                             BOOT_DM15_STATUS_OPERATION_FAILED,
                             request.length,
                             BOOT_ERROR_UNSUPPORTED_OPERATION);
            break;
    }
}

static void fail_write(boot_error_t error)
{
    const uint8_t owner = context.owner_address;
    const uint16_t length = context.operation_length;

    (void)boot_metadata_mark_invalid();
    context.app_valid = false;
    reset_tp_expectation();
    context.operation = BOOT_DM_OPERATION_NONE;
    context.dm16_expected = false;
    context.close_pending = false;
    set_state(BOOTLOADER_STATE_WAIT_HOST, boot_platform_millis());
    (void)queue_dm15(owner,
                     BOOT_DM15_STATUS_OPERATION_FAILED,
                     length,
                     error);
}

static void commit_dm16(const uint8_t *payload,
                        uint16_t payload_length,
                        uint32_t now)
{
    boot_dm16_payload_t data;
    boot_error_t error;
    size_t write_length;
    boot_flash_status_t status;
    uint16_t index;

    if (!context.dm16_expected ||
        !boot_protocol_decode_dm16(payload, payload_length, &data, &error))
    {
        (void)queue_dm15(context.owner_address,
                         BOOT_DM15_STATUS_OPERATION_FAILED,
                         context.operation_length,
                         context.dm16_expected ? error :
                                                 BOOT_ERROR_INVALID_STATE);
        return;
    }
    if (data.data_length != context.operation_length)
    {
        (void)queue_dm15(context.owner_address,
                         BOOT_DM15_STATUS_OPERATION_FAILED,
                         context.operation_length,
                         BOOT_ERROR_INVALID_SIZE);
        return;
    }

    for (index = 0U; index < data.data_length; ++index)
    {
        context.block_buffer[index] = data.data[index];
    }
    write_length = data.data_length;
    while ((write_length % BOOT_FLASH_PROGRAM_UNIT) != 0U)
    {
        context.block_buffer[write_length] = UINT8_C(0xFF);
        ++write_length;
    }

    status = boot_flash_write(BOOT_FLASH_REGION_APPLICATION,
                              context.bytes_committed,
                              context.block_buffer,
                              write_length);
    if (status != BOOT_FLASH_STATUS_OK)
    {
        fail_write(BOOT_ERROR_FLASH_WRITE);
        return;
    }

    context.bytes_committed += data.data_length;
    context.last_operation = BOOT_DM_OPERATION_WRITE;
    context.last_operation_pointer = context.operation_pointer;
    context.last_operation_length = context.operation_length;
    context.last_operation_valid = true;
    context.operation = BOOT_DM_OPERATION_NONE;
    context.dm16_expected = false;
    context.close_pending = true;
    context.last_activity_ms = now;
    boot_j1939_tp_rx_cancel(&context.tp_receiver);
    (void)boot_j1939_tp_rx_allow_bam(&context.tp_receiver,
                                     BOOT_J1939_PGN_COMMANDED_ADDRESS,
                                     context.commanded_address_buffer,
                                     sizeof(context.commanded_address_buffer));
    (void)queue_dm15(context.owner_address,
                     BOOT_DM15_STATUS_OPERATION_COMPLETED,
                     context.last_operation_length,
                     BOOT_ERROR_NONE);
}

static void handle_direct_dm16(const boot_can_frame_t *frame, uint32_t now)
{
    boot_j1939_header_t header;

    if (!boot_j1939_decode_frame(frame, &header) ||
        !header.destination_specific ||
        (header.destination_address != local_address()) ||
        (header.source_address != context.owner_address))
    {
        return;
    }
    commit_dm16(frame->data, frame->dlc, now);
}

static void process_commanded_address(uint32_t now)
{
    const uint8_t *payload = boot_j1939_tp_rx_data(&context.tp_receiver);
    const uint16_t size = boot_j1939_tp_rx_size(&context.tp_receiver);
    uint8_t requested_address;

    if ((payload == NULL) || (size != 9U) || update_state_active() ||
        (boot_protocol_read_u64_le(payload) != context.device_config.name))
    {
        return;
    }
    requested_address = payload[8];
    if (requested_address >= BOOT_J1939_NULL_ADDRESS)
    {
        return;
    }
    if (boot_device_config_store_preferred_address(requested_address) !=
        BOOT_FLASH_STATUS_OK)
    {
        return;
    }

    boot_device_config_load(&context.device_config);
    if (!boot_j1939_address_claim_restart(&context.address_claim,
                                           requested_address,
                                           now) ||
        !apply_claim_address_change(now))
    {
        set_state(BOOTLOADER_STATE_ERROR, now);
        return;
    }
    collect_claim_tx();
}

static void process_tp_event(boot_j1939_tp_event_t event, uint32_t now)
{
    const uint32_t pgn = boot_j1939_tp_rx_pgn(&context.tp_receiver);
    const uint8_t source = boot_j1939_tp_rx_source(&context.tp_receiver);

    if (event == BOOT_J1939_TP_EVENT_COMPLETE)
    {
        if ((pgn == BOOT_J1939_PGN_DM16) && context.dm16_expected &&
            (source == context.owner_address))
        {
            commit_dm16(boot_j1939_tp_rx_data(&context.tp_receiver),
                        boot_j1939_tp_rx_size(&context.tp_receiver),
                        now);
        }
        else if (pgn == BOOT_J1939_PGN_COMMANDED_ADDRESS)
        {
            process_commanded_address(now);
        }
    }
    else if ((event == BOOT_J1939_TP_EVENT_ABORTED) ||
             (event == BOOT_J1939_TP_EVENT_TIMEOUT) ||
             (event == BOOT_J1939_TP_EVENT_REJECTED))
    {
        if (context.dm16_expected && (pgn == BOOT_J1939_PGN_DM16) &&
            (source == context.owner_address))
        {
            reset_tp_expectation();
            context.operation = BOOT_DM_OPERATION_NONE;
            context.dm16_expected = false;
            context.close_pending = false;
            (void)queue_dm15(context.owner_address,
                             BOOT_DM15_STATUS_OPERATION_FAILED,
                             context.operation_length,
                             (event == BOOT_J1939_TP_EVENT_TIMEOUT) ?
                             BOOT_ERROR_TIMEOUT : BOOT_ERROR_INVALID_SEQUENCE);
        }
    }
}

static void handle_tp_frame(const boot_can_frame_t *frame, uint32_t now)
{
    boot_can_frame_t response;
    boot_j1939_tp_event_t event = BOOT_J1939_TP_EVENT_NONE;
    bool response_ready = false;

    if (boot_j1939_tp_rx_handle(&context.tp_receiver,
                                frame,
                                now,
                                &response,
                                &response_ready,
                                &event))
    {
        if (context.dm16_expected &&
            (boot_j1939_tp_rx_pgn(&context.tp_receiver) ==
             BOOT_J1939_PGN_DM16) &&
            (boot_j1939_tp_rx_source(&context.tp_receiver) ==
             context.owner_address))
        {
            context.last_activity_ms = now;
        }
        if (response_ready)
        {
            (void)queue_frame(&response);
        }
        process_tp_event(event, now);
    }
}

static void service_tp(uint32_t now)
{
    boot_can_frame_t response;
    boot_j1939_tp_event_t event = BOOT_J1939_TP_EVENT_NONE;
    bool response_ready = false;

    boot_j1939_tp_rx_task(&context.tp_receiver,
                          now,
                          &response,
                          &response_ready,
                          &event);
    if (response_ready)
    {
        (void)queue_frame(&response);
    }
    process_tp_event(event, now);
}

static void service_erase(void)
{
    const uint32_t erase_length =
        (uint32_t)context.operation_length * BOOT_FLASH_PAGE_SIZE;
    boot_flash_status_t status;

    if (!context.erase_pending || (context.tx_count != 0U) ||
        !boot_can_tx_idle())
    {
        return;
    }

    context.erase_pending = false;
    context.app_valid = false;
    status = boot_metadata_begin(context.expected_size, context.expected_crc32);
    if (status == BOOT_FLASH_STATUS_OK)
    {
        context.flash_was_erased = true;
        status = boot_flash_erase(BOOT_FLASH_REGION_APPLICATION,
                                  0U,
                                  erase_length);
    }
    if (status != BOOT_FLASH_STATUS_OK)
    {
        const uint8_t owner = context.owner_address;
        const uint16_t length = context.operation_length;

        if (context.flash_was_erased)
        {
            (void)boot_metadata_mark_invalid();
        }
        clear_update();
        set_state(BOOTLOADER_STATE_WAIT_HOST, boot_platform_millis());
        (void)queue_dm15(owner,
                         BOOT_DM15_STATUS_OPERATION_FAILED,
                         length,
                         BOOT_ERROR_FLASH_ERASE);
        return;
    }

    context.last_operation = BOOT_DM_OPERATION_ERASE;
    context.last_operation_pointer = context.operation_pointer;
    context.last_operation_length = context.operation_length;
    context.last_operation_valid = true;
    context.operation = BOOT_DM_OPERATION_NONE;
    context.close_pending = true;
    context.last_activity_ms = boot_platform_millis();
    set_state(BOOTLOADER_STATE_RECEIVE, context.last_activity_ms);
    (void)queue_dm15(context.owner_address,
                     BOOT_DM15_STATUS_OPERATION_COMPLETED,
                     context.last_operation_length,
                     BOOT_ERROR_NONE);
}

static void service_address_claim(uint32_t now)
{
    boot_j1939_address_claim_task(&context.address_claim, now);
    if (!apply_claim_address_change(now))
    {
        return;
    }
    collect_claim_tx();
}

static void service_led(uint32_t now)
{
    uint32_t interval;
    bool force_on = false;
    bool force_off = false;

    switch (context.state)
    {
        case BOOTLOADER_STATE_BOOT_WINDOW:
            force_on = true;
            interval = BOOT_LED_WAIT_MS;
            break;
        case BOOTLOADER_STATE_WAIT_HOST:
            interval = BOOT_LED_WAIT_MS;
            break;
        case BOOTLOADER_STATE_PREPARE_UPDATE:
        case BOOTLOADER_STATE_ERASE:
        case BOOTLOADER_STATE_RECEIVE:
        case BOOTLOADER_STATE_VERIFY:
            interval = BOOT_LED_FAST_MS;
            break;
        case BOOTLOADER_STATE_READY:
        case BOOTLOADER_STATE_RUN_APPLICATION:
        case BOOTLOADER_STATE_RESET:
            force_off = true;
            interval = BOOT_LED_WAIT_MS;
            break;
        case BOOTLOADER_STATE_ERROR:
        case BOOTLOADER_STATE_INIT:
        default:
            interval = BOOT_LED_ERROR_MS;
            break;
    }

    if (force_on)
    {
        context.led_on = true;
        boot_platform_led_set(true);
    }
    else if (force_off)
    {
        context.led_on = false;
        boot_platform_led_set(false);
    }
    else if (elapsed_ms(now, context.last_led_ms) >= interval)
    {
        context.last_led_ms = now;
        context.led_on = !context.led_on;
        boot_platform_led_set(context.led_on);
    }
}

static void service_update_timeout(uint32_t now)
{
    uint8_t owner;
    uint8_t session;

    if (!update_state_active() ||
        (elapsed_ms(now, context.last_activity_ms) < BOOT_TRANSFER_TIMEOUT_MS))
    {
        return;
    }

    owner = context.owner_address;
    session = context.session;
    invalidate_update(now);
    (void)queue_control_response(owner,
                                 BOOT_RESPONSE_NACK,
                                 BOOT_COMMAND_ABORT,
                                 session,
                                 BOOT_ERROR_TIMEOUT);
}

static void dispatch_frame(const boot_can_frame_t *frame, uint32_t now)
{
    boot_j1939_header_t header;

    (void)boot_j1939_address_claim_handle(&context.address_claim, frame, now);
    if (!apply_claim_address_change(now))
    {
        return;
    }
    collect_claim_tx();

    if (!boot_j1939_decode_frame(frame, &header))
    {
        return;
    }
    if ((header.pgn == BOOT_J1939_PGN_ADDRESS_CLAIMED) ||
        (header.pgn == BOOT_J1939_PGN_REQUEST))
    {
        return;
    }
    if ((header.pgn == BOOT_J1939_PGN_TP_CM) ||
        (header.pgn == BOOT_J1939_PGN_TP_DT))
    {
        handle_tp_frame(frame, now);
        return;
    }
    if (!boot_j1939_address_claim_is_operational(&context.address_claim))
    {
        return;
    }
    if ((header.pgn == BOOT_J1939_PGN_PROPRIETARY_A) &&
        header.destination_specific &&
        (header.destination_address == local_address()))
    {
        handle_control(frame, now);
    }
    else if ((header.pgn == BOOT_J1939_PGN_DM14) &&
             header.destination_specific &&
             (header.destination_address == local_address()))
    {
        handle_dm14(frame, now);
    }
    else if ((header.pgn == BOOT_J1939_PGN_DM16) &&
             header.destination_specific &&
             (header.destination_address == local_address()))
    {
        handle_direct_dm16(frame, now);
    }
}

void bootloader_init(void)
{
    boot_metadata_t metadata;
    uint32_t now = boot_platform_millis();

    context = (bootloader_context_t){0};
    context.state = BOOTLOADER_STATE_INIT;
    context.state_started_ms = now;
    context.last_activity_ms = now;
    context.last_led_ms = now;
    context.owner_address = BOOT_J1939_NULL_ADDRESS;
    boot_platform_led_set(true);
    context.led_on = true;

    if (boot_flash_init() != BOOT_FLASH_STATUS_OK)
    {
        set_state(BOOTLOADER_STATE_ERROR, boot_platform_millis());
        return;
    }

    boot_device_config_load(&context.device_config);
    boot_j1939_address_claim_init(&context.address_claim,
                                  context.device_config.name,
                                  context.device_config.preferred_address);
    boot_j1939_tp_rx_init(&context.tp_receiver);
    (void)boot_j1939_tp_rx_allow_bam(&context.tp_receiver,
                                     BOOT_J1939_PGN_COMMANDED_ADDRESS,
                                     context.commanded_address_buffer,
                                     sizeof(context.commanded_address_buffer));

    context.boot_requested = boot_platform_take_boot_request();
    context.can_filter_address = context.device_config.preferred_address;
    if (!boot_can_init(context.device_config.preferred_address) ||
        !boot_can_start() ||
        !boot_j1939_address_claim_start(&context.address_claim,
                                        boot_platform_millis()))
    {
        set_state(BOOTLOADER_STATE_ERROR, boot_platform_millis());
        return;
    }
    collect_claim_tx();

    context.app_valid = boot_image_validate(&metadata);
    now = boot_platform_millis();
    context.last_activity_ms = now;
    set_state(context.app_valid ? BOOTLOADER_STATE_BOOT_WINDOW :
                                  BOOTLOADER_STATE_WAIT_HOST,
              now);
}

void bootloader_task(void)
{
    uint32_t now = boot_platform_millis();
    uint32_t budget;

    service_led(now);
    if (context.state == BOOTLOADER_STATE_ERROR)
    {
        return;
    }

    service_address_claim(now);
    service_tp(now);
    flush_tx_queue();

    for (budget = 0U; budget < BOOT_RX_BUDGET_PER_TASK; ++budget)
    {
        boot_can_frame_t frame;
        const boot_can_status_t receive_status = boot_can_receive(&frame);

        if (receive_status == BOOT_CAN_STATUS_EMPTY)
        {
            break;
        }
        if (receive_status != BOOT_CAN_STATUS_OK)
        {
            break;
        }
        now = boot_platform_millis();
        dispatch_frame(&frame, now);
        if (!tx_queue_has_space())
        {
            break;
        }
    }

    now = boot_platform_millis();
    service_address_claim(now);
    service_tp(now);
    flush_tx_queue();
    service_erase();
    flush_tx_queue();
    now = boot_platform_millis();
    service_update_timeout(now);
    flush_tx_queue();

    if ((context.state == BOOTLOADER_STATE_BOOT_WINDOW) &&
        (elapsed_ms(now, context.state_started_ms) >=
         (context.boot_requested ? BOOT_REQUEST_WINDOW_MS : BOOT_WINDOW_MS)))
    {
        boot_can_stop();
        boot_image_jump();
    }

    if ((context.state == BOOTLOADER_STATE_RUN_APPLICATION) &&
        (((context.tx_count == 0U) &&
          (elapsed_ms(now, context.state_started_ms) >= BOOT_RUN_RESPONSE_DELAY_MS) &&
          boot_can_tx_idle()) ||
         (elapsed_ms(now, context.state_started_ms) >= BOOT_RUN_RESPONSE_TIMEOUT_MS)))
    {
        boot_can_stop();
        boot_image_jump();
    }

    if ((context.state == BOOTLOADER_STATE_RESET) &&
        (((context.tx_count == 0U) &&
          (elapsed_ms(now, context.state_started_ms) >= BOOT_RUN_RESPONSE_DELAY_MS) &&
          boot_can_tx_idle()) ||
         (elapsed_ms(now, context.state_started_ms) >= BOOT_RUN_RESPONSE_TIMEOUT_MS)))
    {
        boot_can_stop();
        boot_platform_reset();
    }
}

bootloader_state_t bootloader_get_state(void)
{
    return context.state;
}
