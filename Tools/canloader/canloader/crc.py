"""Firmware CRC implementation.

The wire protocol uses CRC-32/ISO-HDLC (poly 0x04C11DB7, reflected input and
output, init/final XOR 0xFFFFFFFF). Python's zlib.crc32 implements the same
byte-oriented result.
"""

import zlib


def crc32_iso_hdlc(data: bytes | bytearray | memoryview) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF
