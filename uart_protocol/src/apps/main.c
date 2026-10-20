#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include <stdint.h>

#define AHT10_ADDR 0x38

#define I2C_PORT i2c0
#define I2C_SDA  0
#define I2C_SCL  1


// Hardware configuration
#define ADC_NUM 0
#define ADC_PIN 26

// Wokwi NTC Sensor Constants
const float BETA = 3950.0f;       // Wokwi default Beta coefficient
const float T0 = 298.15f;         // Nominal temperature in Kelvin (25°C)
const float R0 = 10000.0f;        // Nominal resistance at 25°C (10k ohms)
const float RFIX = 10000.0f;      // Fixed pull-down/internal resistor (10k ohms)
const float V_REF = 3.3f;         // Pico ADC reference voltage



#define BUFFER_SIZE 100
#define HAS_ANCHOR 1
#define NIBBLED 1 << 1

uint8_t buffer[BUFFER_SIZE] = {0};

bool aht10_init(i2c_inst_t *i2c);
int16_t aht10_get_temp(i2c_inst_t *i2c);




void store_step(uint8_t buffer[BUFFER_SIZE], int16_t steps, uint8_t * flags, int * p){
  // printf("%016b\n", steps);
  printf("FLAGS: %08b\n", *flags);
  if((*flags & NIBBLED)){
   printf("store 2nd half\n");
    buffer[*p] = buffer[*p] | steps;
    (*p)++;
    *flags = *flags & ~(NIBBLED);
  }else{
    

    printf("store 1st half\n");

    buffer[*p] = buffer[*p] | steps;
    buffer[*p] = buffer[*p] << 4;
    *flags = *flags | (NIBBLED);
  }
}


void store_anchor_t(uint8_t buffer[BUFFER_SIZE], int16_t t,uint8_t * flags, int * p){
  store_step(buffer, 0b0000000000001000, flags, p);
  // TODO: Allow storing starting from a nibble
  for (int shift = 12; shift >= 0; shift -= 4) {
      if (*p < BUFFER_SIZE) {
          // Shift the target nibble to the bottom and mask it
          store_step(buffer, (int16_t)((t >> shift) & 0b1111), flags, p);
      }
  }
}

int main() {
    // Initialize standard I/O for printing to console
    stdio_init_all();

    // Initialize ADC hardware
    // adc_init();
    // adc_gpio_init(ADC_PIN);
    // adc_select_input(ADC_NUM);


    i2c_init(I2C_PORT, 100 * 1000); // 100kHz, standard mode — AHT10 doesn't support fast mode well
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    if (!aht10_init(I2C_PORT)) {
        printf("AHT10 init FAILED\n");
    }

    printf("Pico SDK NTC Temperature Sensor initialized.\n");

    int pointer = 0;
    uint8_t flags = 0;
    int16_t last_anchor = 0;

    while (true) {
        // Read the 12-bit raw value from the ADC (0 to 4095)
        int16_t t = aht10_get_temp(I2C_PORT);
        printf("Temp in c: %d\n", t);
        printf("Pointer: %d\n", pointer);

        if(pointer >=BUFFER_SIZE){
          pointer = 0;
        }

        if((flags & HAS_ANCHOR) != 1){
          printf("No anchor\n");
          store_anchor_t(buffer, t, &flags, &pointer);
          flags = flags | HAS_ANCHOR;
          last_anchor = t;
        }else{
          printf("anchor: %d\n", last_anchor);
          int16_t diff  = t - last_anchor;
          printf("temp diff : %d\n", diff);
          int16_t steps = 0;
          int16_t denom = 25;
          if (diff >= 0) {
            steps = (diff + denom / 2) / denom;
          }else{
            steps = (diff - denom / 2) / denom;
          }
          printf("steps: %d\n", steps);
           if (steps < -7 || steps > 7) {
              printf("Step too big\n");
              flags = flags & ~(HAS_ANCHOR);
              // store_anchor_t(buffer, t, &flags, &pointer);

          }else{
            store_step(buffer, steps, &flags, &pointer);

          }
        }

        for(int i = 0; i < BUFFER_SIZE; i++){
          printf("%08b ", buffer[i]);
        }
        printf("\n");
        printf("==============\n");
        // Delay 1 second before the next read loop
        sleep_ms(1000);
    }
}



// Call once, after i2c_init(), before the first read.
bool aht10_init(i2c_inst_t *i2c) {
    sleep_ms(20); // AHT10 needs ~20-40ms after power-up before any command

    uint8_t cmd[] = {0xE1, 0x08, 0x00}; // calibration/init command
    int ret = i2c_write_blocking(i2c, AHT10_ADDR, cmd, 3, false);
    sleep_ms(10);
    return ret == 3;
}

// Triggers a measurement, waits for it, and returns temperature as
// hundredths of a degree C (x100 fixed point). Returns INT16_MIN on
// I2C failure or if the sensor reports "still busy".
int16_t aht10_get_temp(i2c_inst_t *i2c) {
    uint8_t trigger[] = {0xAC, 0x33, 0x00}; // trigger measurement
    if (i2c_write_blocking(i2c, AHT10_ADDR, trigger, 3, false) != 3) {
        return INT16_MIN;
    }

    sleep_ms(80); // measurement takes up to ~75ms per datasheet

    uint8_t data[6];
    if (i2c_read_blocking(i2c, AHT10_ADDR, data, 6, false) != 6) {
        return INT16_MIN;
    }

    if (data[0] & 0x80) {       // bit 7 of status byte = busy
        return INT16_MIN;        // measurement wasn't ready in time
    }

    // Temperature is the low 20 bits, spread across data[3..5]:
    // data[3] low nibble | data[4] | data[5]
    uint32_t raw_temp = ((uint32_t)(data[3] & 0x0F) << 16)
                       | ((uint32_t)data[4] << 8)
                       |  (uint32_t)data[5];

    // T(C) = (raw / 2^20) * 200 - 50
    // Rearranged to integer math, scaled by 100, with round-to-nearest:
    int64_t numerator = (int64_t)raw_temp * 20000 - (int64_t)5000 * 1048576;
    int16_t temp_x100 = (int16_t)((numerator + (numerator >= 0 ? 524288 : -524288)) / 1048576);

    return temp_x100;
}