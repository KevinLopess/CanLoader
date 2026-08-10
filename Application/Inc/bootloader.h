#ifndef BOOTLOADER_H
#define BOOTLOADER_H

typedef enum
{
    BOOTLOADER_STATE_INIT = 0,
    BOOTLOADER_STATE_BOOT_WINDOW,
    BOOTLOADER_STATE_WAIT_HOST,
    BOOTLOADER_STATE_PREPARE_UPDATE,
    BOOTLOADER_STATE_ERASE,
    BOOTLOADER_STATE_RECEIVE,
    BOOTLOADER_STATE_VERIFY,
    BOOTLOADER_STATE_READY,
    BOOTLOADER_STATE_RUN_APPLICATION,
    BOOTLOADER_STATE_ERROR
} bootloader_state_t;

void bootloader_init(void);
void bootloader_task(void);
bootloader_state_t bootloader_get_state(void);

#endif /* BOOTLOADER_H */
