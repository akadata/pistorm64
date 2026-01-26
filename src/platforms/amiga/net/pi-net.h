// SPDX-License-Identifier: MIT

void pinet_init(const char* dev);
void pinet_shutdown(void);
void handle_pinet_write(uint32_t addr, uint32_t val, uint8_t type);
uint32_t handle_pinet_read(uint32_t addr, uint8_t type);
