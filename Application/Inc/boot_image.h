#ifndef BOOT_IMAGE_H
#define BOOT_IMAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_metadata.h"

bool boot_image_calculate_crc(uint32_t image_size, uint32_t *crc32);
bool boot_image_vectors_valid(uint32_t image_size,
                              uint32_t *initial_stack_pointer,
                              uint32_t *reset_handler);
bool boot_image_validate_candidate(uint32_t image_size, uint32_t expected_crc32);
bool boot_image_validate(boot_metadata_t *metadata);
void boot_image_jump(void) __attribute__((noreturn));

#endif /* BOOT_IMAGE_H */
