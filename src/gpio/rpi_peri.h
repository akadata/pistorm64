#pragma once
#include <stdint.h>
#include <stddef.h>

uint32_t rpi_detect_peri_base(void);
int rpi_open_devmem(void);
volatile uint32_t *rpi_map_block(int mem_fd, uint32_t phys_addr, size_t len, uint32_t *page_off_out);

