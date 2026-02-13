// SPDX-License-Identifier: MIT
#ifndef PISTORM_RTG64_TRANSPORT_H
#define PISTORM_RTG64_TRANSPORT_H

#include <stdint.h>

#include "rtg64-protocol.h"

typedef struct rtg64_transport {
  uint32_t regs[64];
  uint32_t pending_cmd;
  uint64_t command_count;
  uint64_t write_count;
  uint8_t open;
} rtg64_transport_t;

typedef struct rtg64_transport_command {
  uint32_t cmd;
  uint32_t arg[8];
} rtg64_transport_command_t;

int rtg64_transport_open(rtg64_transport_t *transport);
void rtg64_transport_close(rtg64_transport_t *transport);
void rtg64_transport_reset(rtg64_transport_t *transport);

uint32_t rtg64_transport_read(rtg64_transport_t *transport, uint32_t offset, uint8_t access_size);
void rtg64_transport_write(rtg64_transport_t *transport,
                           uint32_t offset,
                           uint32_t value,
                           uint8_t access_size);

int rtg64_transport_pop_command(rtg64_transport_t *transport, rtg64_transport_command_t *cmd);
void rtg64_transport_complete(rtg64_transport_t *transport, uint32_t result, uint32_t status_flags);

#endif
