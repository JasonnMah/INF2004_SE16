#include "aht10.h"
#include "pico/stdlib.h"

#define AHT10_ADDR        0x38
#define I2C_TIMEOUT_US    20000u

#define CMD_SOFT_RESET    0xBA
#define CMD_INIT          0xE1      /* followed by 0x08 0x00 */
#define CMD_MEASURE       0xAC      /* followed by 0x33 0x00 */

#define STATUS_BUSY       0x80
#define STATUS_CALIBRATED 0x08

#define T_MIN_CENTI       (-4000)
#define T_MAX_CENTI       (8500)

static bool wr(i2c_inst_t *i2c, const uint8_t *buf, size_t n) {
    return i2c_write_timeout_us(i2c, AHT10_ADDR, buf, n, false, I2C_TIMEOUT_US) == (int)n;
}

static bool rd(i2c_inst_t *i2c, uint8_t *buf, size_t n) {
    return i2c_read_timeout_us(i2c, AHT10_ADDR, buf, n, false, I2C_TIMEOUT_US) == (int)n;
}

aht10_status_t aht10_init(i2c_inst_t *i2c) {
    sleep_ms(40);                                   /* power-up time */
    const uint8_t reset = CMD_SOFT_RESET;
    if (!wr(i2c, &reset, 1)) return AHT10_ERR_I2C;
    sleep_ms(20);

    const uint8_t init[3] = { CMD_INIT, 0x08, 0x00 };
    if (!wr(i2c, init, 3)) return AHT10_ERR_I2C;
    sleep_ms(10);

    uint8_t status;
    if (!rd(i2c, &status, 1)) return AHT10_ERR_I2C;
    return (status & STATUS_CALIBRATED) ? AHT10_OK : AHT10_ERR_NOT_CAL;
}

aht10_status_t aht10_read_temp(i2c_inst_t *i2c, int16_t *t_centi) {
    const uint8_t trigger[3] = { CMD_MEASURE, 0x33, 0x00 };
    if (!wr(i2c, trigger, 3)) return AHT10_ERR_I2C;
    sleep_ms(80);                                   /* conversion takes about 75 ms */

    uint8_t d[6];
    for (int attempt = 0; attempt < 4; attempt++) {
        if (!rd(i2c, d, 6)) return AHT10_ERR_I2C;
        if (!(d[0] & STATUS_BUSY)) break;
        if (attempt == 3) return AHT10_ERR_BUSY;
        sleep_ms(10);
    }
    if (!(d[0] & STATUS_CALIBRATED)) return AHT10_ERR_NOT_CAL;

    /* d[1..3]: humidity (20 bits) and the top 4 temperature bits; d[3] low nibble + d[4], d[5]. */
    uint32_t raw = ((uint32_t)(d[3] & 0x0F) << 16) | ((uint32_t)d[4] << 8) | d[5];
    /* T = raw / 2^20 * 200 - 50, in 0.01 C, rounded */
    int32_t t = (int32_t)(((uint64_t)raw * 20000u + (1u << 19)) >> 20) - 5000;
    if (t < T_MIN_CENTI || t > T_MAX_CENTI) return AHT10_ERR_RANGE;

    *t_centi = (int16_t)t;
    return AHT10_OK;
}

const char *aht10_status_str(aht10_status_t s) {
    switch (s) {
        case AHT10_OK:          return "ok";
        case AHT10_ERR_I2C:     return "I2C error";
        case AHT10_ERR_BUSY:    return "busy";
        case AHT10_ERR_NOT_CAL: return "not calibrated";
        case AHT10_ERR_RANGE:   return "out of range";
    }
    return "?";
}
