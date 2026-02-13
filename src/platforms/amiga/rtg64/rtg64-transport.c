// SPDX-License-Identifier: MIT
#include <string.h>

#include "rtg64-transport.h"

#define RTG64_REG_INDEX(off) (((off) & 0xFFu) >> 2)

static uint32_t mask_by_size(uint32_t current, uint32_t value, uint8_t access_size) {
  switch (access_size) {
  case 1:
    return (current & 0xFFFFFF00u) | (value & 0xFFu);
  case 2:
    return (current & 0xFFFF0000u) | (value & 0xFFFFu);
  default:
    return value;
  }
}

int rtg64_transport_open(rtg64_transport_t *transport) {
  if (!transport) {
    return -1;
  }

  memset(transport, 0, sizeof(*transport));
  transport->open = 1;
  transport->regs[RTG64_REG_INDEX(RTG64_REG_MAGIC)] = RTG64_PROTOCOL_MAGIC;
  transport->regs[RTG64_REG_INDEX(RTG64_REG_VERSION)] = RTG64_PROTOCOL_VERSION;
  transport->regs[RTG64_REG_INDEX(RTG64_REG_FEATURES)] = RTG64_FEATURE_MAILBOX |
                                                          RTG64_FEATURE_SHARED_FB |
                                                          RTG64_FEATURE_PALETTE |
                                                          RTG64_FEATURE_BENCH;
  transport->regs[RTG64_REG_INDEX(RTG64_REG_STATUS)] = RTG64_STATUS_READY;
  return 0;
}

void rtg64_transport_close(rtg64_transport_t *transport) {
  if (!transport) {
    return;
  }
  transport->open = 0;
}

void rtg64_transport_reset(rtg64_transport_t *transport) {
  if (!transport || !transport->open) {
    return;
  }

  const uint64_t command_count = transport->command_count;
  const uint64_t write_count = transport->write_count;
  rtg64_transport_open(transport);
  transport->command_count = command_count;
  transport->write_count = write_count;
}

uint32_t rtg64_transport_read(rtg64_transport_t *transport, uint32_t offset, uint8_t access_size) {
  if (!transport || !transport->open || offset >= RTG64_MMIO_SIZE) {
    return 0;
  }

  uint32_t value = transport->regs[RTG64_REG_INDEX(offset)];
  if (access_size == 1) {
    return value & 0xFFu;
  }
  if (access_size == 2) {
    return value & 0xFFFFu;
  }
  return value;
}

void rtg64_transport_write(rtg64_transport_t *transport,
                           uint32_t offset,
                           uint32_t value,
                           uint8_t access_size) {
  if (!transport || !transport->open || offset >= RTG64_MMIO_SIZE) {
    return;
  }

  transport->write_count++;

  if (offset == RTG64_REG_CMD) {
    uint32_t cmd = value;
    if (access_size == 1) {
      cmd &= 0xFFu;
    } else if (access_size == 2) {
      cmd &= 0xFFFFu;
    }
    transport->regs[RTG64_REG_INDEX(RTG64_REG_CMD)] = cmd;
    transport->pending_cmd = cmd;
    return;
  }

  const uint32_t idx = RTG64_REG_INDEX(offset);
  transport->regs[idx] = mask_by_size(transport->regs[idx], value, access_size);
}

int rtg64_transport_pop_command(rtg64_transport_t *transport, rtg64_transport_command_t *cmd) {
  if (!transport || !cmd || transport->pending_cmd == RTG64_CMD_NONE) {
    return 0;
  }

  memset(cmd, 0, sizeof(*cmd));
  cmd->cmd = transport->pending_cmd;
  cmd->arg[0] = transport->regs[RTG64_REG_INDEX(RTG64_REG_ARG0)];
  cmd->arg[1] = transport->regs[RTG64_REG_INDEX(RTG64_REG_ARG1)];
  cmd->arg[2] = transport->regs[RTG64_REG_INDEX(RTG64_REG_ARG2)];
  cmd->arg[3] = transport->regs[RTG64_REG_INDEX(RTG64_REG_ARG3)];
  cmd->arg[4] = transport->regs[RTG64_REG_INDEX(RTG64_REG_ARG4)];
  cmd->arg[5] = transport->regs[RTG64_REG_INDEX(RTG64_REG_ARG5)];
  cmd->arg[6] = transport->regs[RTG64_REG_INDEX(RTG64_REG_ARG6)];
  cmd->arg[7] = transport->regs[RTG64_REG_INDEX(RTG64_REG_ARG7)];

  transport->command_count++;
  transport->pending_cmd = RTG64_CMD_NONE;
  transport->regs[RTG64_REG_INDEX(RTG64_REG_CMD)] = RTG64_CMD_NONE;
  return 1;
}

void rtg64_transport_complete(rtg64_transport_t *transport, uint32_t result, uint32_t status_flags) {
  if (!transport || !transport->open) {
    return;
  }

  transport->regs[RTG64_REG_INDEX(RTG64_REG_RESULT)] = result;
  transport->regs[RTG64_REG_INDEX(RTG64_REG_STATUS)] = status_flags;
}
