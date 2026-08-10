#ifndef BOOT_PLATFORM_H
#define BOOT_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

uint32_t boot_platform_millis(void);
void boot_platform_led_set(bool on);
void boot_platform_prepare_handoff(void);
void boot_platform_jump(uint32_t vector_address,
                        uint32_t initial_stack_pointer,
                        uint32_t reset_handler) __attribute__((noreturn));
void boot_platform_reset(void) __attribute__((noreturn));

#endif /* BOOT_PLATFORM_H */
