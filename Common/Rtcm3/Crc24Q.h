#pragma once
#include <stddef.h>
#include <stdint.h>

uint32_t crc24qUpdate(uint32_t crc, const uint8_t *data, size_t length);
uint32_t crc24q(const uint8_t *data, size_t length);
