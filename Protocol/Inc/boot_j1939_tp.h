#ifndef BOOT_J1939_TP_H
#define BOOT_J1939_TP_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_can.h"

#define BOOT_J1939_TP_CONTROL_RTS        UINT8_C(16)
#define BOOT_J1939_TP_CONTROL_CTS        UINT8_C(17)
#define BOOT_J1939_TP_CONTROL_EOM_ACK    UINT8_C(19)
#define BOOT_J1939_TP_CONTROL_BAM        UINT8_C(32)
#define BOOT_J1939_TP_CONTROL_ABORT      UINT8_C(255)

#define BOOT_J1939_TP_ABORT_BUSY         UINT8_C(1)
#define BOOT_J1939_TP_ABORT_RESOURCES    UINT8_C(2)
#define BOOT_J1939_TP_ABORT_TIMEOUT      UINT8_C(3)
#define BOOT_J1939_TP_MAX_MESSAGE_SIZE   UINT16_C(1785)

typedef enum
{
    BOOT_J1939_TP_EVENT_NONE = 0,
    BOOT_J1939_TP_EVENT_STARTED,
    BOOT_J1939_TP_EVENT_COMPLETE,
    BOOT_J1939_TP_EVENT_ABORTED,
    BOOT_J1939_TP_EVENT_TIMEOUT,
    BOOT_J1939_TP_EVENT_REJECTED
} boot_j1939_tp_event_t;

typedef enum
{
    BOOT_J1939_TP_RX_IDLE = 0,
    BOOT_J1939_TP_RX_RTS_CTS,
    BOOT_J1939_TP_RX_BAM
} boot_j1939_tp_rx_state_t;

typedef struct
{
    boot_j1939_tp_rx_state_t state;
    uint32_t pgn;
    uint32_t deadline_ms;
    uint16_t message_size;
    uint16_t bytes_received;
    uint16_t expected_rts_size;
    uint16_t buffer_capacity;
    uint16_t bam_capacity;
    uint8_t source_address;
    uint8_t destination_address;
    uint8_t total_packets;
    uint8_t next_sequence;
    uint8_t packets_until_cts;
    uint8_t sender_max_packets;
    uint8_t expected_rts_source;
    uint8_t expected_rts_destination;
    uint32_t expected_rts_pgn;
    uint32_t allowed_bam_pgn;
    uint8_t *buffer;
    uint8_t *expected_rts_buffer;
    uint8_t *bam_buffer;
    bool expected_rts_valid;
    bool bam_allowed;
} boot_j1939_tp_rx_t;

void boot_j1939_tp_rx_init(boot_j1939_tp_rx_t *receiver);
bool boot_j1939_tp_rx_expect_rts(boot_j1939_tp_rx_t *receiver,
                                 uint8_t source_address,
                                 uint8_t destination_address,
                                 uint32_t pgn,
                                 uint16_t message_size,
                                 uint8_t *buffer,
                                 uint16_t capacity);
bool boot_j1939_tp_rx_allow_bam(boot_j1939_tp_rx_t *receiver,
                                uint32_t pgn,
                                uint8_t *buffer,
                                uint16_t capacity);
bool boot_j1939_tp_rx_handle(boot_j1939_tp_rx_t *receiver,
                             const boot_can_frame_t *frame,
                             uint32_t now_ms,
                             boot_can_frame_t *response,
                             bool *response_ready,
                             boot_j1939_tp_event_t *event);
void boot_j1939_tp_rx_task(boot_j1939_tp_rx_t *receiver,
                           uint32_t now_ms,
                           boot_can_frame_t *response,
                           bool *response_ready,
                           boot_j1939_tp_event_t *event);
void boot_j1939_tp_rx_cancel(boot_j1939_tp_rx_t *receiver);
const uint8_t *boot_j1939_tp_rx_data(const boot_j1939_tp_rx_t *receiver);
uint16_t boot_j1939_tp_rx_size(const boot_j1939_tp_rx_t *receiver);
uint32_t boot_j1939_tp_rx_pgn(const boot_j1939_tp_rx_t *receiver);
uint8_t boot_j1939_tp_rx_source(const boot_j1939_tp_rx_t *receiver);

#endif /* BOOT_J1939_TP_H */
