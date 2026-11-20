#ifndef UI_TEST_STDLIB_H
#define UI_TEST_STDLIB_H
#include <stdbool.h>
#include <stdint.h>
#define GPIO_IN false
#define GPIO_OUT true
uint64_t time_us_64(void);
void gpio_init(unsigned pin);
void gpio_set_dir(unsigned pin, bool output);
void gpio_pull_up(unsigned pin);
void gpio_put(unsigned pin, bool value);
bool gpio_get(unsigned pin);
#endif
