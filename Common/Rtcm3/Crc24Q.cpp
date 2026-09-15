#include "Crc24Q.h"

uint32_t crc24qUpdate(uint32_t crc, const uint8_t *data, size_t length) {
    constexpr uint32_t polynomial = 0x1864CFBU;
    for(size_t i = 0; i < length; ++i) {
        crc ^= static_cast<uint32_t>(data[i]) << 16;
        for(uint8_t bit = 0; bit < 8; ++bit) {
            crc <<= 1;
            if(crc & 0x1000000U) crc ^= polynomial;
        }
    }
    return crc & 0xFFFFFFU;
}

uint32_t crc24q(const uint8_t *data, size_t length) { return crc24qUpdate(0, data, length); }
