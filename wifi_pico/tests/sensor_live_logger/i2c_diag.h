/* Optional startup report for the separately built I2C diagnostic firmware.
 * It explains which initialization read/write returned an error or short count. */
#ifndef SENSOR_I2C_DIAG_H
#define SENSOR_I2C_DIAG_H
/* Show the original driver's saved startup transactions; never repeats them. */
void sensor_i2c_trace_report(void);
#endif
