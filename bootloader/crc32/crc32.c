#include "crc32.h"

#define CRC32_POLY 0xEDB88320

uint32_t crc32(const uint8_t *data, size_t size, uint32_t crc)
{
    crc ^= 0xFFFFFFFF;

    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];

        for (size_t j = 0; j < 8; ++j) {
            if ((crc & 1) != 0) {
                crc = (crc >> 1) ^ CRC32_POLY;
            }
            else {
                crc >>= 1;
            }
        }
    }

    return crc ^ 0xFFFFFFFF;
}
