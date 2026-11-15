#pragma once
#include <stdint.h>
#include <stddef.h>
#define FLASH_PAGE_SIZE       256u
#define FLASH_SECTOR_SIZE     4096u
#define PICO_FLASH_SIZE_BYTES (2u * 1024u * 1024u)
/* "char" and an incomplete size on purpose: the build maps flash_log.c's __flash_binary_end onto
 * fake_flash_mem[<image end>], which redeclares this array with a size (see CMakeLists.txt). */
extern char fake_flash_mem[];
#define XIP_BASE ((uintptr_t)fake_flash_mem)             /* the "memory-mapped" flash is this array */
void flash_range_program(uint32_t flash_offs, const uint8_t *data, size_t count);
void flash_range_erase(uint32_t flash_offs, size_t count);
