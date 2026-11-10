#include <assert.h>
#include <stdio.h>
#include "app_activity.h"
#include "boot_j1939.h"

static boot_can_frame_t sent;
static bool accept_tx = true;
static unsigned int calls;

static bool enqueue(const boot_can_frame_t *frame)
{
    ++calls;
    sent = *frame;
    return accept_tx;
}

static void test_exchange_and_validation(void)
{
    app_activity_t a, b;
    boot_j1939_header_t header;
    uint32_t value;
    const uint32_t rx_at = APP_LED_RX_START_MS + 50U;
    app_activity_init(&a, 0);
    app_activity_init(&b, 0);
    calls = 0;
    app_activity_service(&a, 99, 0x80, 0xF1, true, enqueue);
    assert(calls == 0);
    app_activity_service(&a, 100, 0x80, 0xF1, true, enqueue);
    assert(calls == 1 && a.tx_queued == 1);
    assert(boot_j1939_decode_frame(&sent, &header));
    assert(header.pgn == 0xFFFF && header.priority == 7);
    assert(header.source_address == 0x80 && !header.destination_specific);
    assert(!app_activity_receive(&a, &sent, 0x80, 100));
    assert(app_activity_receive(&b, &sent, 0x81, rx_at));
    assert(b.rx_valid == 1 && b.last_rx_sequence == 0);
    assert(b.last_rx_address == 0x80);
    assert(app_activity_led_on(&b, rx_at));
    assert(APP_LED_SOLID ==
           app_activity_led_on(&b, rx_at + APP_ACTIVITY_RX_PULSE_MS));
    assert(app_activity_info(&b, APP_INFO_LAST_RX_AGE_MS, rx_at + 300U, &value));
    assert(value == 300);
    assert(app_activity_info(&a, APP_INFO_LAST_RX_AGE_MS, 700, &value));
    assert(value == UINT32_MAX);
    assert(!app_activity_info(&b, 0x7F, 700, &value));
    assert(!app_activity_info(&b, APP_INFO_RX_VALID, 700, NULL));

    const boot_can_frame_t valid = sent;
    sent.data[2] = 2;
    assert(!app_activity_receive(&b, &sent, 0x81, 450));
    sent = valid; sent.data[0] = 'X';
    assert(!app_activity_receive(&b, &sent, 0x81, 450));
    sent = valid; sent.data[3] = 0xFF;
    assert(!app_activity_receive(&b, &sent, 0x81, 450));
    sent = valid; sent.dlc = 7;
    assert(!app_activity_receive(&b, &sent, 0x81, 450));
    sent = valid; sent.id_type = BOOT_CAN_ID_STANDARD;
    assert(!app_activity_receive(&b, &sent, 0x81, 450));
    sent = valid; sent.id = (sent.id & ~0xFFU) | 0xFEU;
    assert(!app_activity_receive(&b, &sent, 0x81, 450));
    sent = valid; sent.id = 0x18EEFF80;
    assert(!app_activity_receive(&b, &sent, 0x81, 450));
    assert(b.rx_valid == 1);

    app_activity_service(&b, 500, 0x81, 0xB1, true, enqueue);
    assert(app_activity_receive(&a, &sent, 0x80, 500));
    assert(a.last_rx_address == 0x81);
}

static void test_backpressure_claim_and_reset(void)
{
    app_activity_t state;
    app_activity_init(&state, 0);
    calls = 0;
    app_activity_service(&state, 100, 0x80, 0xF1, false, enqueue);
    assert(calls == 0 && state.tx_queued == 0);
    app_activity_service(&state, 1000, 0xFE, 0xF1, true, enqueue);
    assert(calls == 0);
    accept_tx = false;
    app_activity_service(&state, 2000, 0x80, 0xF1, true, enqueue);
    assert(calls == 1 && state.tx_skipped == 1 && state.tx_queued == 0);
    accept_tx = true;
    app_activity_service(&state, 2001, 0x80, 0xF1, true, enqueue);
    assert(calls == 1); /* No catch-up burst after a busy queue. */
    app_activity_service(&state, 2100, 0x80, 0xF1, true, enqueue);
    assert(state.tx_queued == 1 && sent.data[4] == 0);
    app_activity_service(&state, 2200, 0x80, 0xF1, true, enqueue);
    assert(state.tx_queued == 2 && sent.data[4] == 1);
    app_activity_init(&state, 2300);
    assert(state.tx_queued == 0 && state.tx_skipped == 0 && !state.rx_seen);
}

static void test_wrap_and_local_led(void)
{
    app_activity_t state;
    uint32_t value;
    app_activity_init(&state, UINT32_MAX - 49U);
    calls = 0;
    app_activity_service(&state, 49, 0x80, 0xF1, true, enqueue);
    assert(calls == 0);
    app_activity_service(&state, 50, 0x80, 0xF1, true, enqueue);
    assert(calls == 1);
    assert(app_activity_receive(&state, &sent, 0x81, UINT32_MAX - 9U));
    assert(app_activity_info(&state, APP_INFO_LAST_RX_AGE_MS, 5, &value));
    assert(value == 15);
    app_activity_init(&state, 0);
    if (APP_LED_SOLID)
    {
        for (uint32_t t = 0; t < 2000U; ++t)
        {
            assert(app_activity_led_on(&state, t));
        }
        return;
    }
    for (uint32_t pulse = 0; pulse < APP_LED_PULSES; ++pulse)
    {
        assert(app_activity_led_on(&state, pulse * 180U));
        assert(app_activity_led_on(&state, (pulse * 180U) + 79U));
        assert(!app_activity_led_on(&state, (pulse * 180U) + 80U));
    }
    assert(!app_activity_led_on(&state, APP_LED_PULSES * 180U));
    assert(!app_activity_led_on(&state, 999));
    assert(app_activity_led_on(&state, 1000));
}

int main(void)
{
    test_exchange_and_validation();
    test_backpressure_claim_and_reset();
    test_wrap_and_local_led();
    printf("test_app_activity (variant %u): OK\n", (unsigned)APP_VARIANT);
    return 0;
}
