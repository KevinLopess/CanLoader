#include "bootloader.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boot_can.h"
#include "boot_config.h"
#include "boot_device_config.h"
#include "boot_flash.h"
#include "boot_image.h"
#include "boot_metadata.h"
#include "boot_platform.h"
#include "boot_protocol.h"

typedef struct
{
    bootloader_state_t state;
    uint32_t state_started_ms;
    uint32_t last_activity_ms;
    uint32_t last_led_ms;
    uint32_t expected_size;
    uint32_t expected_crc32;
    uint32_t bytes_committed;
    uint16_t next_block_index;
    uint16_t last_block_index;
    uint8_t session;
    uint8_t current_block_sequence;
    uint8_t current_block_length;
    uint8_t expected_frame_sequence;
    uint8_t block_bytes_received;
    uint8_t last_block_sequence;
    uint8_t last_block_length;
    uint8_t node_id;
    bool app_valid;
    bool boot_requested;
    bool crc_received;
    bool block_active;
    bool last_block_valid;
    bool response_pending;
    bool led_on;
    boot_can_frame_t pending_response;
    uint8_t block_buffer[BOOT_TRANSFER_BLOCK_SIZE + BOOT_FLASH_PROGRAM_UNIT - 1U];
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

static void clear_transfer(void)
{
    context.expected_size = 0U;
    context.expected_crc32 = 0U;
    context.bytes_committed = 0U;
    context.next_block_index = 0U;
    context.last_block_index = 0U;
    context.session = 0U;
    context.current_block_sequence = 0U;
    context.current_block_length = 0U;
    context.expected_frame_sequence = 0U;
    context.block_bytes_received = 0U;
    context.last_block_sequence = 0U;
    context.last_block_length = 0U;
    context.crc_received = false;
    context.block_active = false;
    context.last_block_valid = false;
}

static bool command_known(boot_command_t command)
{
    switch (command)
    {
        case BOOT_COMMAND_DISCOVER:
        case BOOT_COMMAND_INFO:
        case BOOT_COMMAND_PROBE:
        case BOOT_COMMAND_SET_NODE_ID:
        case BOOT_COMMAND_START_UPDATE:
        case BOOT_COMMAND_SET_IMAGE_CRC:
        case BOOT_COMMAND_ERASE:
        case BOOT_COMMAND_BEGIN_BLOCK:
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

static void queue_response(boot_response_kind_t kind,
                           boot_command_t command,
                           uint8_t session,
                           uint32_t value)
{
    if (context.response_pending)
    {
        return;
    }

    boot_protocol_make_response(&context.pending_response,
                                context.node_id,
                                kind,
                                command,
                                session,
                                value);
    context.response_pending = true;
}

static void queue_ack(boot_command_t command, uint8_t session, uint32_t value)
{
    queue_response(BOOT_RESPONSE_ACK, command, session, value);
}

static void queue_nack(boot_command_t command, uint8_t session, boot_error_t error)
{
    queue_response(BOOT_RESPONSE_NACK, command, session, (uint32_t)error);
}

static void flush_response(void)
{
    if (!context.response_pending)
    {
        return;
    }

    if (boot_can_send(&context.pending_response) == BOOT_CAN_STATUS_OK)
    {
        context.response_pending = false;
    }
}

static bool session_matches(uint8_t session)
{
    return (session != 0U) && (session == context.session);
}

static uint32_t capability_word(void)
{
    uint32_t flags = context.app_valid ? BOOT_CAPABILITY_APPLICATION_VALID : 0U;

    if ((context.state == BOOTLOADER_STATE_PREPARE_UPDATE) ||
        (context.state == BOOTLOADER_STATE_ERASE) ||
        (context.state == BOOTLOADER_STATE_RECEIVE) ||
        (context.state == BOOTLOADER_STATE_VERIFY))
    {
        flags |= BOOT_CAPABILITY_UPDATE_ACTIVE;
    }

    return flags |
           ((uint32_t)context.node_id << 8U) |
           ((uint32_t)BOOT_PROTOCOL_VERSION << 16U) |
           ((uint32_t)BOOT_TARGET_CODE << 24U);
}

static void handle_info(const boot_protocol_request_t *request)
{
    boot_metadata_t metadata;
    uint32_t value;

    if (request->parameter != 0U)
    {
        queue_nack(request->command, request->session, BOOT_ERROR_MALFORMED_FRAME);
        return;
    }

    switch ((boot_info_selector_t)request->value)
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

        default:
            queue_nack(request->command, request->session, BOOT_ERROR_INVALID_COMMAND);
            return;
    }

    queue_ack(request->command, request->session, value);
}

static void handle_probe(const boot_protocol_request_t *request)
{
    uint32_t value;

    if (request->value != 0U)
    {
        queue_nack(request->command, request->session, BOOT_ERROR_MALFORMED_FRAME);
        return;
    }

    switch ((boot_probe_selector_t)request->parameter)
    {
        case BOOT_PROBE_CAPABILITIES:
            value = capability_word();
            break;

        case BOOT_PROBE_UID_WORD_0:
            value = boot_platform_uid_word(0U);
            break;

        case BOOT_PROBE_UID_WORD_1:
            value = boot_platform_uid_word(1U);
            break;

        case BOOT_PROBE_UID_WORD_2:
            value = boot_platform_uid_word(2U);
            break;

        default:
            queue_nack(request->command, request->session, BOOT_ERROR_INVALID_COMMAND);
            return;
    }

    queue_ack(request->command, request->session, value);
}

static void handle_set_node_id(const boot_protocol_request_t *request, uint32_t now)
{
    const uint32_t requested_node = request->value;
    boot_flash_status_t status;

    if ((request->parameter != 0U) ||
        (requested_node < BOOT_MIN_NODE_ID) ||
        (requested_node > BOOT_MAX_NODE_ID))
    {
        queue_nack(request->command, request->session, BOOT_ERROR_INVALID_SIZE);
        return;
    }
    if ((context.state != BOOTLOADER_STATE_BOOT_WINDOW) &&
        (context.state != BOOTLOADER_STATE_WAIT_HOST) &&
        (context.state != BOOTLOADER_STATE_READY))
    {
        queue_nack(request->command, request->session, BOOT_ERROR_INVALID_STATE);
        return;
    }

    if ((uint8_t)requested_node == context.node_id)
    {
        queue_ack(request->command, request->session, requested_node);
        return;
    }

    status = boot_device_config_store_node((uint8_t)requested_node);
    if (status != BOOT_FLASH_STATUS_OK)
    {
        queue_nack(request->command,
                   request->session,
                   (status == BOOT_FLASH_STATUS_ERASE) ? BOOT_ERROR_FLASH_ERASE :
                                                         BOOT_ERROR_FLASH_WRITE);
        return;
    }

    boot_platform_set_boot_request();
    queue_ack(request->command, request->session, requested_node);
    set_state(BOOTLOADER_STATE_RESET, now);
}

static void handle_start_update(const boot_protocol_request_t *request, uint32_t now)
{
    if ((request->session == 0U) || (request->parameter != 0U))
    {
        queue_nack(request->command, request->session, BOOT_ERROR_SESSION);
        return;
    }

    if (context.state == BOOTLOADER_STATE_PREPARE_UPDATE)
    {
        if ((request->session == context.session) &&
            (request->value == context.expected_size))
        {
            queue_ack(request->command, request->session, BOOT_TRANSFER_BLOCK_SIZE);
        }
        else
        {
            queue_nack(request->command, request->session, BOOT_ERROR_INVALID_STATE);
        }
        return;
    }

    if ((context.state != BOOTLOADER_STATE_BOOT_WINDOW) &&
        (context.state != BOOTLOADER_STATE_WAIT_HOST) &&
        (context.state != BOOTLOADER_STATE_READY))
    {
        queue_nack(request->command, request->session, BOOT_ERROR_INVALID_STATE);
        return;
    }
    if ((request->value < 8U) || (request->value > BOOT_APPLICATION_MAX_SIZE))
    {
        queue_nack(request->command, request->session, BOOT_ERROR_INVALID_SIZE);
        return;
    }

    clear_transfer();
    context.session = request->session;
    context.expected_size = request->value;
    context.last_activity_ms = now;
    set_state(BOOTLOADER_STATE_PREPARE_UPDATE, now);
    queue_ack(request->command, request->session, BOOT_TRANSFER_BLOCK_SIZE);
}

static void handle_set_crc(const boot_protocol_request_t *request)
{
    if ((context.state != BOOTLOADER_STATE_PREPARE_UPDATE) ||
        !session_matches(request->session) || (request->parameter != 0U))
    {
        queue_nack(request->command, request->session,
                   session_matches(request->session) ? BOOT_ERROR_INVALID_STATE :
                                                       BOOT_ERROR_SESSION);
        return;
    }

    if (context.crc_received && (context.expected_crc32 != request->value))
    {
        queue_nack(request->command, request->session, BOOT_ERROR_INVALID_STATE);
        return;
    }

    context.expected_crc32 = request->value;
    context.crc_received = true;
    queue_ack(request->command, request->session, request->value);
}

static void handle_erase(const boot_protocol_request_t *request, uint32_t now)
{
    uint32_t erase_length;
    boot_flash_status_t status;

    if ((context.state == BOOTLOADER_STATE_RECEIVE) &&
        session_matches(request->session) && (context.bytes_committed == 0U) &&
        !context.block_active)
    {
        queue_ack(request->command, request->session, context.expected_size);
        return;
    }

    if ((context.state != BOOTLOADER_STATE_PREPARE_UPDATE) ||
        !session_matches(request->session) || !context.crc_received ||
        (request->parameter != 0U) || (request->value != 0U))
    {
        queue_nack(request->command, request->session,
                   session_matches(request->session) ? BOOT_ERROR_INVALID_STATE :
                                                       BOOT_ERROR_SESSION);
        return;
    }

    set_state(BOOTLOADER_STATE_ERASE, now);
    context.app_valid = false;
    status = boot_metadata_begin(context.expected_size, context.expected_crc32);
    if (status != BOOT_FLASH_STATUS_OK)
    {
        set_state(BOOTLOADER_STATE_WAIT_HOST, boot_platform_millis());
        queue_nack(request->command, request->session, BOOT_ERROR_FLASH_ERASE);
        clear_transfer();
        return;
    }

    erase_length = ((context.expected_size + BOOT_FLASH_PAGE_SIZE - 1U) /
                    BOOT_FLASH_PAGE_SIZE) * BOOT_FLASH_PAGE_SIZE;
    status = boot_flash_erase(BOOT_FLASH_REGION_APPLICATION, 0U, erase_length);
    if (status != BOOT_FLASH_STATUS_OK)
    {
        (void)boot_metadata_mark_invalid();
        set_state(BOOTLOADER_STATE_WAIT_HOST, boot_platform_millis());
        queue_nack(request->command, request->session, BOOT_ERROR_FLASH_ERASE);
        clear_transfer();
        return;
    }

    now = boot_platform_millis();
    context.last_activity_ms = now;
    set_state(BOOTLOADER_STATE_RECEIVE, now);
    queue_ack(request->command, request->session, context.expected_size);
}

static uint8_t expected_block_length(void)
{
    const uint32_t remaining = context.expected_size - context.bytes_committed;

    return (uint8_t)((remaining > BOOT_TRANSFER_BLOCK_SIZE) ?
                     BOOT_TRANSFER_BLOCK_SIZE : remaining);
}

static void reset_current_block(const boot_protocol_block_t *block)
{
    context.current_block_sequence = block->block_sequence;
    context.current_block_length = block->block_length;
    context.expected_frame_sequence = 0U;
    context.block_bytes_received = 0U;
    context.block_active = true;
}

static void handle_begin_block(const boot_protocol_request_t *request)
{
    boot_protocol_block_t block;
    boot_error_t error;

    if ((context.state != BOOTLOADER_STATE_RECEIVE) ||
        !session_matches(request->session))
    {
        queue_nack(request->command, request->session,
                   session_matches(request->session) ? BOOT_ERROR_INVALID_STATE :
                                                       BOOT_ERROR_SESSION);
        return;
    }

    if (!boot_protocol_decode_block(request, &block, &error))
    {
        queue_nack(request->command, request->session, error);
        return;
    }

    if (context.last_block_valid &&
        (block.block_index == context.last_block_index) &&
        (block.block_sequence == context.last_block_sequence) &&
        (block.block_length == context.last_block_length) &&
        ((uint16_t)(block.block_index + 1U) == context.next_block_index))
    {
        queue_ack(request->command, request->session, context.bytes_committed);
        return;
    }

    if ((block.block_index != context.next_block_index) ||
        (block.block_sequence != (uint8_t)block.block_index) ||
        (block.block_length != expected_block_length()))
    {
        queue_nack(request->command, request->session, BOOT_ERROR_INVALID_SEQUENCE);
        return;
    }

    reset_current_block(&block);
    queue_ack(request->command, request->session, context.bytes_committed);
}

static bool payload_matches_buffer(const boot_protocol_data_t *data,
                                   uint32_t buffer_offset)
{
    uint8_t index;

    for (index = 0U; index < data->payload_length; ++index)
    {
        if (context.block_buffer[buffer_offset + index] != data->payload[index])
        {
            return false;
        }
    }
    return true;
}

static void fail_receive(boot_command_t command, boot_error_t error)
{
    const uint8_t session = context.session;

    (void)boot_metadata_mark_invalid();
    clear_transfer();
    context.app_valid = false;
    set_state(BOOTLOADER_STATE_WAIT_HOST, boot_platform_millis());
    queue_nack(command, session, error);
}

static void commit_current_block(void)
{
    size_t write_length = context.current_block_length;
    boot_flash_status_t status;

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
        fail_receive(BOOT_COMMAND_DATA, BOOT_ERROR_FLASH_WRITE);
        return;
    }

    context.last_block_index = context.next_block_index;
    context.last_block_sequence = context.current_block_sequence;
    context.last_block_length = context.current_block_length;
    context.last_block_valid = true;
    context.bytes_committed += context.current_block_length;
    ++context.next_block_index;
    context.block_active = false;
    queue_ack(BOOT_COMMAND_DATA, context.session, context.bytes_committed);
}

static void handle_data_frame(const boot_can_frame_t *frame, uint32_t now)
{
    boot_protocol_data_t data;
    boot_error_t error;
    uint32_t buffer_offset;
    uint8_t expected_payload_length;

    if (!boot_protocol_decode_data(frame, context.node_id, &data, &error))
    {
        queue_nack(BOOT_COMMAND_DATA,
                   (frame->dlc > 0U) ? frame->data[0] : 0U,
                   error);
        return;
    }

    if ((context.state != BOOTLOADER_STATE_RECEIVE) ||
        !session_matches(data.session) || !context.block_active)
    {
        queue_nack(BOOT_COMMAND_DATA, data.session,
                   session_matches(data.session) ? BOOT_ERROR_INVALID_STATE :
                                                   BOOT_ERROR_SESSION);
        return;
    }
    if (data.block_sequence != context.current_block_sequence)
    {
        queue_nack(BOOT_COMMAND_DATA, data.session, BOOT_ERROR_INVALID_SEQUENCE);
        return;
    }

    buffer_offset = (uint32_t)data.frame_sequence * BOOT_DATA_BYTES_PER_FRAME;
    if (buffer_offset >= context.current_block_length)
    {
        queue_nack(BOOT_COMMAND_DATA, data.session, BOOT_ERROR_INVALID_SEQUENCE);
        return;
    }

    expected_payload_length = (uint8_t)(context.current_block_length - buffer_offset);
    if (expected_payload_length > BOOT_DATA_BYTES_PER_FRAME)
    {
        expected_payload_length = BOOT_DATA_BYTES_PER_FRAME;
    }
    if (data.payload_length != expected_payload_length)
    {
        queue_nack(BOOT_COMMAND_DATA, data.session, BOOT_ERROR_INVALID_SIZE);
        return;
    }

    if (data.frame_sequence < context.expected_frame_sequence)
    {
        if (!payload_matches_buffer(&data, buffer_offset))
        {
            queue_nack(BOOT_COMMAND_DATA, data.session, BOOT_ERROR_INVALID_SEQUENCE);
        }
        return;
    }
    if (data.frame_sequence != context.expected_frame_sequence)
    {
        queue_nack(BOOT_COMMAND_DATA, data.session, BOOT_ERROR_INVALID_SEQUENCE);
        return;
    }

    {
        uint8_t index;

        for (index = 0U; index < data.payload_length; ++index)
        {
            context.block_buffer[buffer_offset + index] = data.payload[index];
        }
    }

    context.block_bytes_received = (uint8_t)(context.block_bytes_received +
                                              data.payload_length);
    ++context.expected_frame_sequence;
    context.last_activity_ms = now;

    if (context.block_bytes_received == context.current_block_length)
    {
        commit_current_block();
    }
}

static void handle_finish(const boot_protocol_request_t *request, uint32_t now)
{
    uint32_t computed_crc;
    uint32_t stack_pointer;
    uint32_t reset_handler;
    boot_metadata_t metadata;

    if ((request->parameter != 0U) || (request->value != 0U))
    {
        queue_nack(request->command, request->session, BOOT_ERROR_MALFORMED_FRAME);
        return;
    }

    if ((context.state == BOOTLOADER_STATE_READY) &&
        session_matches(request->session) && context.app_valid &&
        boot_metadata_read(&metadata))
    {
        queue_ack(request->command, request->session, metadata.image_crc32);
        return;
    }

    if ((context.state != BOOTLOADER_STATE_RECEIVE) ||
        !session_matches(request->session) || context.block_active ||
        (context.bytes_committed != context.expected_size))
    {
        queue_nack(request->command, request->session,
                   session_matches(request->session) ? BOOT_ERROR_INVALID_STATE :
                                                       BOOT_ERROR_SESSION);
        return;
    }

    set_state(BOOTLOADER_STATE_VERIFY, now);
    if (!boot_image_calculate_crc(context.expected_size, &computed_crc) ||
        (computed_crc != context.expected_crc32))
    {
        fail_receive(BOOT_COMMAND_FINISH, BOOT_ERROR_CRC_MISMATCH);
        return;
    }
    if (!boot_image_vectors_valid(context.expected_size,
                                  &stack_pointer,
                                  &reset_handler))
    {
        fail_receive(BOOT_COMMAND_FINISH, BOOT_ERROR_INVALID_APPLICATION);
        return;
    }
    if (boot_metadata_mark_valid() != BOOT_FLASH_STATUS_OK)
    {
        fail_receive(BOOT_COMMAND_FINISH, BOOT_ERROR_FLASH_WRITE);
        return;
    }
    if (!boot_image_validate(&metadata))
    {
        fail_receive(BOOT_COMMAND_FINISH, BOOT_ERROR_INVALID_APPLICATION);
        return;
    }

    context.app_valid = true;
    set_state(BOOTLOADER_STATE_READY, boot_platform_millis());
    queue_ack(request->command, request->session, computed_crc);
}

static void handle_verify(const boot_protocol_request_t *request)
{
    boot_metadata_t metadata;

    if ((request->parameter != 0U) || (request->value != 0U))
    {
        queue_nack(request->command, request->session, BOOT_ERROR_MALFORMED_FRAME);
        return;
    }

    if (!boot_image_validate(&metadata))
    {
        context.app_valid = false;
        queue_nack(request->command, request->session,
                   BOOT_ERROR_INVALID_APPLICATION);
        return;
    }

    context.app_valid = true;
    queue_ack(request->command, request->session, metadata.image_crc32);
}

static void handle_run(const boot_protocol_request_t *request, uint32_t now)
{
    boot_metadata_t metadata;

    if ((request->parameter != 0U) || (request->value != 0U))
    {
        queue_nack(request->command, request->session, BOOT_ERROR_MALFORMED_FRAME);
        return;
    }

    if (!boot_image_validate(&metadata))
    {
        context.app_valid = false;
        queue_nack(request->command, request->session,
                   BOOT_ERROR_INVALID_APPLICATION);
        return;
    }

    context.app_valid = true;
    queue_ack(request->command, request->session, metadata.image_crc32);
    set_state(BOOTLOADER_STATE_RUN_APPLICATION, now);
}

static void handle_abort(const boot_protocol_request_t *request, uint32_t now)
{
    if ((request->parameter != 0U) || (request->value != 0U))
    {
        queue_nack(request->command, request->session, BOOT_ERROR_MALFORMED_FRAME);
        return;
    }

    if ((context.session != 0U) && !session_matches(request->session))
    {
        queue_nack(request->command, request->session, BOOT_ERROR_SESSION);
        return;
    }

    if ((context.state == BOOTLOADER_STATE_RECEIVE) ||
        (context.state == BOOTLOADER_STATE_ERASE) ||
        (context.state == BOOTLOADER_STATE_VERIFY))
    {
        (void)boot_metadata_mark_invalid();
        context.app_valid = false;
    }

    clear_transfer();
    set_state(BOOTLOADER_STATE_WAIT_HOST, now);
    queue_ack(request->command, request->session, 0U);
}

static void handle_request(const boot_can_frame_t *frame, uint32_t now)
{
    boot_protocol_request_t request;
    boot_error_t error;
    const bool broadcast = frame->id == BOOT_CAN_BROADCAST_ID;

    if (!boot_protocol_decode_request(frame, context.node_id, &request, &error))
    {
        const boot_command_t command = (frame->dlc > 1U) ?
                                       (boot_command_t)frame->data[1] :
                                       BOOT_COMMAND_DISCOVER;
        const uint8_t session = (frame->dlc > 2U) ? frame->data[2] : 0U;
        queue_nack(command, session, error);
        return;
    }

    if (broadcast && (request.command != BOOT_COMMAND_DISCOVER) &&
        (request.command != BOOT_COMMAND_INFO) &&
        (request.command != BOOT_COMMAND_PROBE))
    {
        return;
    }
    if (!command_known(request.command))
    {
        queue_nack(request.command, request.session, BOOT_ERROR_INVALID_COMMAND);
        return;
    }

    context.last_activity_ms = now;
    switch (request.command)
    {
        case BOOT_COMMAND_DISCOVER:
            if ((request.parameter != 0U) || (request.value != 0U))
            {
                queue_nack(request.command, request.session, BOOT_ERROR_MALFORMED_FRAME);
            }
            else
            {
                queue_ack(request.command, request.session, capability_word());
            }
            break;

        case BOOT_COMMAND_INFO:
            handle_info(&request);
            break;

        case BOOT_COMMAND_PROBE:
            handle_probe(&request);
            break;

        case BOOT_COMMAND_SET_NODE_ID:
            handle_set_node_id(&request, now);
            break;

        case BOOT_COMMAND_START_UPDATE:
            handle_start_update(&request, now);
            break;

        case BOOT_COMMAND_SET_IMAGE_CRC:
            handle_set_crc(&request);
            break;

        case BOOT_COMMAND_ERASE:
            handle_erase(&request, now);
            break;

        case BOOT_COMMAND_BEGIN_BLOCK:
            handle_begin_block(&request);
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
                queue_nack(request.command,
                           request.session,
                           BOOT_ERROR_MALFORMED_FRAME);
            }
            else
            {
                set_state(BOOTLOADER_STATE_WAIT_HOST, now);
                queue_ack(request.command, request.session, 0U);
            }
            break;

        case BOOT_COMMAND_ABORT:
            handle_abort(&request, now);
            break;

        default:
            queue_nack(request.command, request.session, BOOT_ERROR_INVALID_COMMAND);
            break;
    }

    if ((context.state == BOOTLOADER_STATE_BOOT_WINDOW) &&
        (request.command != BOOT_COMMAND_PROBE) &&
        context.response_pending &&
        (context.pending_response.data[1] == (uint8_t)BOOT_RESPONSE_ACK))
    {
        set_state(BOOTLOADER_STATE_WAIT_HOST, now);
    }
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

static void service_transfer_timeout(uint32_t now)
{
    if (((context.state != BOOTLOADER_STATE_PREPARE_UPDATE) &&
         (context.state != BOOTLOADER_STATE_RECEIVE)) ||
        (elapsed_ms(now, context.last_activity_ms) < BOOT_TRANSFER_TIMEOUT_MS))
    {
        return;
    }

    {
        const uint8_t session = context.session;
        const bool image_was_erased = context.state == BOOTLOADER_STATE_RECEIVE;

        if (image_was_erased)
        {
            (void)boot_metadata_mark_invalid();
            context.app_valid = false;
        }
        clear_transfer();
        set_state(BOOTLOADER_STATE_WAIT_HOST, now);
        context.response_pending = false;
        queue_nack(BOOT_COMMAND_ABORT, session, BOOT_ERROR_TIMEOUT);
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
    boot_platform_led_set(true);
    context.led_on = true;
    context.node_id = BOOT_DEFAULT_NODE_ID;

    if (boot_flash_init() != BOOT_FLASH_STATUS_OK)
    {
        set_state(BOOTLOADER_STATE_ERROR, boot_platform_millis());
        return;
    }

    context.node_id = boot_device_config_load_node();
    context.boot_requested = boot_platform_take_boot_request();
    if (!boot_can_init(context.node_id) || !boot_can_start())
    {
        set_state(BOOTLOADER_STATE_ERROR, boot_platform_millis());
        return;
    }

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

    flush_response();
    if (!context.response_pending)
    {
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
            if ((frame.id == BOOT_CAN_COMMAND_ID(context.node_id)) ||
                (frame.id == BOOT_CAN_BROADCAST_ID))
            {
                handle_request(&frame, now);
            }
            else if (frame.id == BOOT_CAN_DATA_ID(context.node_id))
            {
                handle_data_frame(&frame, now);
            }

            if (context.response_pending)
            {
                break;
            }
        }
    }

    now = boot_platform_millis();
    service_transfer_timeout(now);

    if ((context.state == BOOTLOADER_STATE_BOOT_WINDOW) &&
        (elapsed_ms(now, context.state_started_ms) >=
         (context.boot_requested ? BOOT_REQUEST_WINDOW_MS : BOOT_WINDOW_MS)))
    {
        boot_can_stop();
        boot_image_jump();
    }

    if ((context.state == BOOTLOADER_STATE_RUN_APPLICATION) &&
        (((!context.response_pending) &&
          (elapsed_ms(now, context.state_started_ms) >= BOOT_RUN_RESPONSE_DELAY_MS) &&
          boot_can_tx_idle()) ||
         (elapsed_ms(now, context.state_started_ms) >= BOOT_RUN_RESPONSE_TIMEOUT_MS)))
    {
        boot_can_stop();
        boot_image_jump();
    }

    if ((context.state == BOOTLOADER_STATE_RESET) &&
        (((!context.response_pending) &&
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
