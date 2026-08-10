#ifndef BOOT_CAN_H
#define BOOT_CAN_H

#include <stdbool.h>
#include <stdint.h>

#define BOOT_CAN_MAX_DATA_LENGTH 8U

typedef struct
{
    uint32_t id;
    uint8_t dlc;
    uint8_t data[BOOT_CAN_MAX_DATA_LENGTH];
} boot_can_frame_t;

typedef enum
{
    BOOT_CAN_STATUS_OK = 0,
    BOOT_CAN_STATUS_EMPTY,
    BOOT_CAN_STATUS_BUSY,
    BOOT_CAN_STATUS_BUS_OFF,
    BOOT_CAN_STATUS_ERROR
} boot_can_status_t;

bool boot_can_init(void);
bool boot_can_start(void);
void boot_can_stop(void);
boot_can_status_t boot_can_send(const boot_can_frame_t *frame);
boot_can_status_t boot_can_receive(boot_can_frame_t *frame);
boot_can_status_t boot_can_get_state(void);
bool boot_can_tx_idle(void);

#endif /* BOOT_CAN_H */
