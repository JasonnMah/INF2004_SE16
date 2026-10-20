#ifndef PROTOCOL_CRC16_H
#define PROTOCOL_CRC16_H

#include <stdint.h>
#include <stddef.h>

uint16_t crc16_x25(const uint8_t *data, size_t length);
#endif
