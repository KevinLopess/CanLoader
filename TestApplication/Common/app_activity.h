#ifndef APP_ACTIVITY_H
#define APP_ACTIVITY_H

#include <stdbool.h>
#include <stdint.h>
#include "boot_can.h"

/* Lab-only broadcast, separate from CanLoader control and memory operations.
 * PS=0xFF also passes the G0 port's existing global-address hardware filter. */
#define APP_ACTIVITY_PGN UINT32_C(0x00FFFF)
#define APP_ACTIVITY_PERIOD_MS UINT32_C(100)
#define APP_ACTIVITY_VERSION 1U
#define APP_ACTIVITY_RX_PULSE_MS UINT32_C(35)

/* Build-time test image selector, so bench images differ by eye:
 * image 1 keeps the original double blink, image 2 keeps the LED solid on
 * and image 3 blinks three times per second. */
#ifndef APP_VARIANT
#define APP_VARIANT 1U
#endif
#if (APP_VARIANT < 1) || (APP_VARIANT > 3)
#error "APP_VARIANT must be between 1 and 3"
#endif
#define APP_LED_SOLID (APP_VARIANT == 2U)
/* Pulses are 80 ms on every 180 ms; peer pulses start 90 ms after the last. */
#define APP_LED_PULSES ((APP_VARIANT == 3U) ? 3U : 2U)
#define APP_LED_RX_START_MS ((APP_LED_PULSES * 180U) - 10U)

typedef enum
{
    APP_INFO_ACTIVITY_VERSION = 0x80,
    APP_INFO_TX_QUEUED,
    APP_INFO_RX_VALID,
    APP_INFO_TX_SKIPPED,
    APP_INFO_LAST_RX_ADDRESS,
    APP_INFO_LAST_RX_SEQUENCE,
    APP_INFO_LAST_RX_AGE_MS,
    APP_INFO_RX_OVERRUNS,
    APP_INFO_CAN_STATUS,
    APP_INFO_PERIOD_MS
} app_activity_info_t;

typedef struct
{
    uint32_t tx_queued;
    uint32_t rx_valid;
    uint32_t tx_skipped;
    uint32_t last_rx_ms;
    uint32_t last_rx_sequence;
    uint32_t next_tx_ms;
    uint32_t rx_overruns;
    uint8_t last_rx_address;
    bool rx_seen;
} app_activity_t;

typedef bool (*app_activity_enqueue_t)(const boot_can_frame_t *frame);

void app_activity_init(app_activity_t *state, uint32_t now);
void app_activity_service(app_activity_t *state, uint32_t now,
                          uint8_t address, uint8_t target, bool enabled,
                          app_activity_enqueue_t enqueue);
bool app_activity_receive(app_activity_t *state, const boot_can_frame_t *frame,
                          uint8_t local_address, uint32_t now);
bool app_activity_led_on(const app_activity_t *state, uint32_t now);
bool app_activity_info(const app_activity_t *state, uint8_t selector,
                       uint32_t now, uint32_t *value);
#endif
