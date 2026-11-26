#ifndef DEMO_MOCK_LOGGER_H
#define DEMO_MOCK_LOGGER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Total demo receive storage, deliberately independent of one frame's limit.
#define DEMO_RECEIVE_CAPACITY 256

const uint8_t *mock_logger_data(void);
size_t mock_logger_length(void);
bool mock_logger_matches(const uint8_t *data, size_t length);

#endif
