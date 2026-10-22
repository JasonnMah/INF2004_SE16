/*
 * aht10.h - AHT10 temperature sensor over I2C (address 0x38).
 *
 * NOTE: the command bytes and timings in aht10.c are from memory of the datasheet. Check them
 * against the datasheet, or wrap your existing working driver behind this same interface.
 */
#ifndef AHT10_H
#define AHT10_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/i2c.h"

typedef enum {
    AHT10_OK = 0,
    AHT10_ERR_I2C,        /* no acknowledge, or a timeout */
    AHT10_ERR_BUSY,       /* busy flag still set after waiting */
    AHT10_ERR_NOT_CAL,    /* calibration flag not set */
    AHT10_ERR_RANGE       /* value outside the rated -40..+85 C: treat as a bad reading */
} aht10_status_t;

/* Soft reset, then the initialisation command. Returns AHT10_OK if calibrated. */
aht10_status_t aht10_init(i2c_inst_t *i2c);

/* One blocking measurement (about 80 ms). t_centi is in units of 0.01 C. */
aht10_status_t aht10_read_temp(i2c_inst_t *i2c, int16_t *t_centi);

const char *aht10_status_str(aht10_status_t s);

#endif
