#ifndef TEMP_LOG_PRINT_H
#define TEMP_LOG_PRINT_H
#include "temp_decoder.h"
void temp_log_print(const uint8_t *blob, size_t length, uint32_t timestamp, uint32_t interval);
void temp_encode_print(int16_t original, const temp_decoded_sample_t *encoding);
#endif
