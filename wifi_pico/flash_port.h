#ifndef WIFI_FLASH_PORT_H
#define WIFI_FLASH_PORT_H
#include "batch_store.h"
/* Application flash region, outside the firmware's linker allocation. This
 * cooperative rendezvous is for the course SMP task model, not SDK lockout. */
#define WIFI_CACHE_FLASH_OFFSET 0x001fc000u
bool flash_port_init(batch_flash_t *flash);
void flash_port_core1_poll(void);
unsigned flash_port_park_count(void);
unsigned flash_port_operation_count(void);
#endif
