#include "app_activity.h"
#include "boot_j1939.h"

void app_activity_init(app_activity_t *state, uint32_t now)
{
    *state = (app_activity_t){0};
    state->next_tx_ms = now + APP_ACTIVITY_PERIOD_MS;
    state->last_rx_address = UINT8_MAX;
}

void app_activity_service(app_activity_t *state, uint32_t now,
                          uint8_t address, uint8_t target, bool enabled,
                          app_activity_enqueue_t enqueue)
{
    boot_can_frame_t frame;
    uint8_t data[8] = {'C', 'L', APP_ACTIVITY_VERSION, target, 0, 0, 0, 0};
    uint8_t i;

    if (!enabled || (address >= 0xFEU))
    {
        /* Stagger peers after claim; never burst to catch up after inactivity. */
        state->next_tx_ms = now + APP_ACTIVITY_PERIOD_MS +
                            (((uint32_t)address * 17U) % APP_ACTIVITY_PERIOD_MS);
        return;
    }
    if ((int32_t)(now - state->next_tx_ms) < 0)
    {
        return;
    }
    state->next_tx_ms = now + APP_ACTIVITY_PERIOD_MS;
    for (i = 0; i < 4U; ++i)
    {
        data[4U + i] = (uint8_t)(state->tx_queued >> (8U * i));
    }
    if (boot_j1939_make_frame(&frame, 7U, APP_ACTIVITY_PGN, address,
                              0xFFU, data, sizeof(data)) && enqueue(&frame))
    {
        ++state->tx_queued;
    }
    else
    {
        ++state->tx_skipped;
    }
}

bool app_activity_receive(app_activity_t *state, const boot_can_frame_t *frame,
                          uint8_t local_address, uint32_t now)
{
    boot_j1939_header_t header;
    uint8_t i;
    uint32_t sequence = 0;

    if (!boot_j1939_decode_frame(frame, &header) ||
        (header.pgn != APP_ACTIVITY_PGN) || (frame->dlc != 8U) ||
        (header.source_address >= 0xFEU) ||
        (header.source_address == local_address) ||
        (frame->data[0] != 'C') || (frame->data[1] != 'L') ||
        (frame->data[2] != APP_ACTIVITY_VERSION) ||
        ((frame->data[3] != 0xF1U) && (frame->data[3] != 0xB1U)))
    {
        return false;
    }
    for (i = 0; i < 4U; ++i)
    {
        sequence |= (uint32_t)frame->data[4U + i] << (8U * i);
    }
    ++state->rx_valid;
    state->last_rx_ms = now;
    state->last_rx_sequence = sequence;
    state->last_rx_address = header.source_address;
    state->rx_seen = true;
    return true;
}

bool app_activity_led_on(const app_activity_t *state, uint32_t now)
{
    const uint32_t phase = now % 1000U;
    /* Local execution: two pulses at the beginning of each second.
     * Peer reception: short pulses only in the rest of that second. */
    return (phase < 80U) || ((phase >= 180U) && (phase < 260U)) ||
           ((phase >= 350U) && state->rx_seen &&
            ((uint32_t)(now - state->last_rx_ms) < APP_ACTIVITY_RX_PULSE_MS));
}

bool app_activity_info(const app_activity_t *state, uint8_t selector,
                       uint32_t now, uint32_t *value)
{
    if (value == 0)
    {
        return false;
    }
    switch (selector)
    {
        case APP_INFO_ACTIVITY_VERSION: *value = APP_ACTIVITY_VERSION; break;
        case APP_INFO_TX_QUEUED: *value = state->tx_queued; break;
        case APP_INFO_RX_VALID: *value = state->rx_valid; break;
        case APP_INFO_TX_SKIPPED: *value = state->tx_skipped; break;
        case APP_INFO_LAST_RX_ADDRESS: *value = state->last_rx_address; break;
        case APP_INFO_LAST_RX_SEQUENCE: *value = state->last_rx_sequence; break;
        case APP_INFO_LAST_RX_AGE_MS:
            *value = state->rx_seen ? now - state->last_rx_ms : UINT32_MAX;
            break;
        case APP_INFO_RX_OVERRUNS: *value = state->rx_overruns; break;
        case APP_INFO_PERIOD_MS: *value = APP_ACTIVITY_PERIOD_MS; break;
        default: return false;
    }
    return true;
}
