#include "boot_crc32.h"

/* CRC-32/ISO-HDLC: reflected polynomial 0xEDB88320, init/xorout 0xFFFFFFFF. */
uint32_t boot_crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{
    size_t index;

    if ((data == NULL) && (length != 0U))
    {
        return crc;
    }

    for (index = 0U; index < length; ++index)
    {
        uint32_t bit;

        crc ^= data[index];
        for (bit = 0U; bit < 8U; ++bit)
        {
            const uint32_t mask = (uint32_t)(-(int32_t)(crc & 1U));
            crc = (crc >> 1U) ^ (UINT32_C(0xEDB88320) & mask);
        }
    }

    return crc;
}

uint32_t boot_crc32_finalize(uint32_t crc)
{
    return crc ^ UINT32_C(0xFFFFFFFF);
}

uint32_t boot_crc32_compute(const uint8_t *data, size_t length)
{
    return boot_crc32_finalize(boot_crc32_update(BOOT_CRC32_INITIAL_VALUE, data, length));
}
