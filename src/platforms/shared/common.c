// SPDX-License-Identifier: MIT

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <endian.h>
#include "platforms/platforms.h"
#include "gpio/ps_protocol.h"
#include "log.h"

void dump_range_to_file(uint32_t addr, uint32_t size, char* filename) {
  FILE* out = fopen(filename, "wb+");
  if (out == NULL) {
    LOG_ERROR("[SHARED-DUMP_RANGE_TO_FILE] Failed to open %s for writing.\n", filename);
    LOG_WARN("[SHARED-DUMP_RANGE_TO_FILE] Memory range has not been dumped to file.\n");
    return;
  }

  for (uint32_t i = 0; i < size; i += 2) {
    uint16_t in = be16toh(read16(addr + i));
    fwrite(&in, 2, 1, out);
  }

  fclose(out);
  LOG_INFO("[SHARED-DUMP_RANGE_TO_FILE] Memory range dumped to file %s.\n", filename);
}

uint8_t* dump_range_to_memory(uint32_t addr, uint32_t size) {
  uint8_t* mem = calloc(size, 1);

  if (mem == NULL) {
    LOG_ERROR("[SHARED-DUMP_RANGE_TO_MEMORY] Failed to allocate memory for dumped range.\n");
    return NULL;
  }

  for (uint32_t i = 0; i < size; i += 2) {
    uint16_t in = (uint16_t)be16toh(read16(addr + i));
    memcpy(&mem[i], &in, sizeof(in));
  }

  LOG_INFO("[SHARED-DUMP_RANGE_TO_FILE] Memory range copied to RAM.\n");
  return mem;
}
