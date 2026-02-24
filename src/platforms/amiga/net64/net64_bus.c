// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <string.h>

#include "config_file/config_file.h"
#include "emulator.h"
#include "log.h"

#include "net64_bus.h"
#include "net64_config.h"
#include "net64_device.h"

extern struct emulator_config *cfg;

typedef struct net64_regs {
  uint32_t cmd;
  uint32_t status;
  uint32_t tx_addr;
  uint32_t tx_len;
  uint32_t rx_addr;
  uint32_t rx_len;
  uint32_t rx_actual;
  uint32_t irq_status;
  uint32_t irq_mask;
  uint32_t mac_lo;
  uint32_t mac_hi;
  uint32_t features;
  uint32_t link;
  uint32_t mtu;
} net64_regs_t;

static net64_regs_t g_net64_regs;
static uint8_t g_net64_initialized;

static int net64_dbg_enabled(uint32_t flag) {
  const net64_config_t *cfg64 = net64_config_get();
  if (cfg64 == NULL || log_get_level() < LOG_LEVEL_DEBUG) {
    return 0;
  }
  return (cfg64->debug_flags & flag) != 0u;
}

static uint32_t net64_type_width(uint8_t type) {
  if (type == OP_TYPE_BYTE) {
    return 1;
  }
  if (type == OP_TYPE_WORD) {
    return 2;
  }
  if (type == OP_TYPE_LONGWORD) {
    return 4;
  }
  return 0;
}

static void net64_copy_from_amiga(uint32_t addr, uint8_t *dst, uint32_t len) {
  if (dst == NULL || len == 0) {
    return;
  }

  uint32_t copied = 0;
  while (copied < len) {
    uint32_t cur_addr = addr + copied;
    int index = get_mapped_item_by_address(cfg, cur_addr);
    if (index >= 0) {
      uint32_t map_end = (uint32_t)cfg->map_high[index];
      uint32_t chunk = map_end - cur_addr;
      uint32_t remain = len - copied;
      if (chunk > remain) {
        chunk = remain;
      }

      uint8_t *src = cfg->map_data[index] + (cur_addr - (uint32_t)cfg->map_offset[index]);
      memcpy(dst + copied, src, chunk);
      copied += chunk;
    } else {
      dst[copied] = (uint8_t)m68k_read_memory_8(cur_addr);
      copied++;
    }
  }
}

static void net64_copy_to_amiga(uint32_t addr, const uint8_t *src, uint32_t len) {
  if (src == NULL || len == 0) {
    return;
  }

  uint32_t copied = 0;
  while (copied < len) {
    uint32_t cur_addr = addr + copied;
    int index = get_mapped_item_by_address(cfg, cur_addr);
    if (index >= 0) {
      uint32_t map_end = (uint32_t)cfg->map_high[index];
      uint32_t chunk = map_end - cur_addr;
      uint32_t remain = len - copied;
      if (chunk > remain) {
        chunk = remain;
      }

      uint8_t *dst = cfg->map_data[index] + (cur_addr - (uint32_t)cfg->map_offset[index]);
      memcpy(dst, src + copied, chunk);
      copied += chunk;
    } else {
      m68k_write_memory_8(cur_addr, src[copied]);
      copied++;
    }
  }
}

static void net64_update_mac_regs(void) {
  uint8_t mac[6];
  net64_device_get_mac(mac);

  g_net64_regs.mac_lo = ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) |
                        ((uint32_t)mac[4] << 8) | (uint32_t)mac[5];
  g_net64_regs.mac_hi = ((uint32_t)mac[0] << 8) | (uint32_t)mac[1];
}

static void net64_apply_mac_regs(void) {
  uint8_t mac[6];
  mac[0] = (uint8_t)((g_net64_regs.mac_hi >> 8) & 0xFFu);
  mac[1] = (uint8_t)(g_net64_regs.mac_hi & 0xFFu);
  mac[2] = (uint8_t)((g_net64_regs.mac_lo >> 24) & 0xFFu);
  mac[3] = (uint8_t)((g_net64_regs.mac_lo >> 16) & 0xFFu);
  mac[4] = (uint8_t)((g_net64_regs.mac_lo >> 8) & 0xFFu);
  mac[5] = (uint8_t)(g_net64_regs.mac_lo & 0xFFu);

  mac[0] &= (uint8_t)~0x01u;
  mac[0] |= 0x02u;

  net64_device_set_mac(mac);
}

static void net64_refresh_status(void) {
  uint32_t status = NET64_STATUS_READY;
  uint32_t pending = net64_device_rx_pending();
  const net64_config_t *cfg64 = net64_config_get();
  uint32_t link_mbps = cfg64 ? cfg64->link_speed_mbps : 1000u;
  int link_up = net64_device_link_up();

  if (link_up) {
    status |= NET64_STATUS_LINK_UP;
  }
  if (pending > 0) {
    status |= NET64_STATUS_RX_PENDING;
  }

  if ((g_net64_regs.status & NET64_STATUS_ONLINE) != 0u) {
    status |= NET64_STATUS_ONLINE;
  }
  if ((g_net64_regs.status & NET64_STATUS_CONNECTED) != 0u) {
    status |= NET64_STATUS_CONNECTED;
  }
  if ((g_net64_regs.status & NET64_STATUS_TX_OK) != 0u) {
    status |= NET64_STATUS_TX_OK;
  }
  if ((g_net64_regs.status & NET64_STATUS_TX_ERR) != 0u) {
    status |= NET64_STATUS_TX_ERR;
  }

  g_net64_regs.status = status;
  g_net64_regs.link = (link_mbps & 0x7FFFFFFFu) | (link_up ? 0x80000000u : 0u);
  g_net64_regs.mtu = 1500u;
}

static void net64_command_reset(void) {
  g_net64_regs.tx_addr = 0;
  g_net64_regs.tx_len = 0;
  g_net64_regs.rx_addr = 0;
  g_net64_regs.rx_len = 0;
  g_net64_regs.rx_actual = 0;
  g_net64_regs.irq_status = 0;
  g_net64_regs.status &= (NET64_STATUS_ONLINE | NET64_STATUS_CONNECTED);

  net64_device_reset_queues();
  net64_update_mac_regs();
  net64_refresh_status();
  if (net64_dbg_enabled(NET64_DBG_CFG)) {
    LOG_DEBUG("[NET64] CMD_RESET complete\n");
  }
}

static void net64_command_tx(void) {
  uint32_t len = g_net64_regs.tx_len;
  if (len == 0 || len > NET64_MAX_FRAME) {
    g_net64_regs.status |= NET64_STATUS_TX_ERR;
    g_net64_regs.irq_status |= NET64_IRQ_TX_EVENT;
    return;
  }

  uint8_t frame[NET64_MAX_FRAME];
  net64_copy_from_amiga(g_net64_regs.tx_addr, frame, len);
  if (net64_dbg_enabled(NET64_DBG_TX)) {
    LOG_DEBUG("[NET64] CMD_TX_KICK addr=0x%08X len=%u\n",
              (unsigned int)g_net64_regs.tx_addr, (unsigned int)len);
  }

  if (net64_device_send_frame(frame, (uint16_t)len) == 0) {
    g_net64_regs.status |= NET64_STATUS_TX_OK;
    g_net64_regs.status &= (uint32_t)~NET64_STATUS_TX_ERR;
  } else {
    g_net64_regs.status |= NET64_STATUS_TX_ERR;
    g_net64_regs.status &= (uint32_t)~NET64_STATUS_TX_OK;
  }

  g_net64_regs.irq_status |= NET64_IRQ_TX_EVENT;
  net64_refresh_status();
}

static void net64_command_rx_pop(void) {
  if (g_net64_regs.rx_len == 0 || g_net64_regs.rx_addr == 0) {
    g_net64_regs.rx_actual = 0;
    return;
  }

  uint8_t frame[NET64_MAX_FRAME];
  uint16_t frame_len = 0;

  if (net64_device_recv_frame(frame, NET64_MAX_FRAME, &frame_len) != 0) {
    g_net64_regs.rx_actual = 0;
    net64_refresh_status();
    return;
  }

  uint32_t copy_len = frame_len;
  if (copy_len > g_net64_regs.rx_len) {
    copy_len = g_net64_regs.rx_len;
  }

  net64_copy_to_amiga(g_net64_regs.rx_addr, frame, copy_len);
  if (net64_dbg_enabled(NET64_DBG_RX)) {
    LOG_DEBUG("[NET64] CMD_RX_POP addr=0x%08X copy=%u frame=%u\n",
              (unsigned int)g_net64_regs.rx_addr, (unsigned int)copy_len, (unsigned int)frame_len);
  }
  g_net64_regs.rx_actual = frame_len;
  g_net64_regs.irq_status |= NET64_IRQ_RX_EVENT;
  net64_refresh_status();
}

static void net64_command_apply_cfg(void) {
  net64_apply_mac_regs();
  net64_device_set_promisc((g_net64_regs.features & NET64_FEATURE_PROMISC) ? 1u : 0u);
  if (net64_dbg_enabled(NET64_DBG_CFG)) {
    LOG_DEBUG("[NET64] CMD_APPLY_CFG features=0x%08X\n", (unsigned int)g_net64_regs.features);
  }
}

static void net64_execute_command(uint32_t cmd) {
  if (!g_net64_initialized) {
    return;
  }

  g_net64_regs.cmd = cmd;

  switch (cmd) {
  case NET64_CMD_NOP:
    break;
  case NET64_CMD_RESET:
    net64_command_reset();
    break;
  case NET64_CMD_TX_KICK:
    net64_command_tx();
    break;
  case NET64_CMD_RX_POP:
    net64_command_rx_pop();
    break;
  case NET64_CMD_APPLY_CFG:
    net64_command_apply_cfg();
    break;
  default:
    break;
  }
}

static uint32_t net64_read_register_u32(uint32_t offset) {
  net64_stats_t stats;

  net64_refresh_status();
  net64_device_get_stats(&stats);

  switch (offset) {
  case NET64_REG_CMD:
    return g_net64_regs.cmd;
  case NET64_REG_STATUS:
    return g_net64_regs.status;
  case NET64_REG_TX_ADDR:
    return g_net64_regs.tx_addr;
  case NET64_REG_TX_LEN:
    return g_net64_regs.tx_len;
  case NET64_REG_RX_ADDR:
    return g_net64_regs.rx_addr;
  case NET64_REG_RX_LEN:
    return g_net64_regs.rx_len;
  case NET64_REG_RX_ACTUAL:
    return g_net64_regs.rx_actual;
  case NET64_REG_IRQ_STATUS:
    return g_net64_regs.irq_status;
  case NET64_REG_IRQ_MASK:
    return g_net64_regs.irq_mask;
  case NET64_REG_MAC_LO:
    return g_net64_regs.mac_lo;
  case NET64_REG_MAC_HI:
    return g_net64_regs.mac_hi;
  case NET64_REG_FEATURES:
    return g_net64_regs.features;
  case NET64_REG_LINK:
    return g_net64_regs.link;
  case NET64_REG_MTU:
    return g_net64_regs.mtu;
  case NET64_REG_RX_PENDING:
    return net64_device_rx_pending();

  case NET64_REG_TX_PKTS_LO:
    return (uint32_t)(stats.tx_packets & 0xFFFFFFFFu);
  case NET64_REG_TX_PKTS_HI:
    return (uint32_t)(stats.tx_packets >> 32);
  case NET64_REG_TX_BYTES_LO:
    return (uint32_t)(stats.tx_bytes & 0xFFFFFFFFu);
  case NET64_REG_TX_BYTES_HI:
    return (uint32_t)(stats.tx_bytes >> 32);
  case NET64_REG_TX_ERRS_LO:
    return (uint32_t)(stats.tx_errors & 0xFFFFFFFFu);
  case NET64_REG_TX_ERRS_HI:
    return (uint32_t)(stats.tx_errors >> 32);
  case NET64_REG_RX_PKTS_LO:
    return (uint32_t)(stats.rx_packets & 0xFFFFFFFFu);
  case NET64_REG_RX_PKTS_HI:
    return (uint32_t)(stats.rx_packets >> 32);
  case NET64_REG_RX_BYTES_LO:
    return (uint32_t)(stats.rx_bytes & 0xFFFFFFFFu);
  case NET64_REG_RX_BYTES_HI:
    return (uint32_t)(stats.rx_bytes >> 32);
  case NET64_REG_RX_DROP_LO:
    return (uint32_t)(stats.rx_dropped & 0xFFFFFFFFu);
  case NET64_REG_RX_DROP_HI:
    return (uint32_t)(stats.rx_dropped >> 32);
  default:
    return 0;
  }
}

static void net64_write_register_u32(uint32_t offset, uint32_t value) {
  if (net64_dbg_enabled(NET64_DBG_REGS)) {
    LOG_DEBUG("[NET64] REG write off=0x%04X val=0x%08X\n",
              (unsigned int)offset, (unsigned int)value);
  }
  switch (offset) {
  case NET64_REG_CMD:
    net64_execute_command(value);
    break;
  case NET64_REG_TX_ADDR:
    g_net64_regs.tx_addr = value;
    break;
  case NET64_REG_TX_LEN:
    g_net64_regs.tx_len = value;
    break;
  case NET64_REG_RX_ADDR:
    g_net64_regs.rx_addr = value;
    break;
  case NET64_REG_RX_LEN:
    g_net64_regs.rx_len = value;
    break;
  case NET64_REG_IRQ_STATUS:
    g_net64_regs.irq_status &= ~value;
    break;
  case NET64_REG_IRQ_MASK:
    g_net64_regs.irq_mask = value;
    break;
  case NET64_REG_MAC_LO:
    g_net64_regs.mac_lo = value;
    break;
  case NET64_REG_MAC_HI:
    g_net64_regs.mac_hi = value;
    break;
  case NET64_REG_FEATURES:
    g_net64_regs.features = value;
    net64_device_set_promisc((value & NET64_FEATURE_PROMISC) ? 1u : 0u);
    break;
  case NET64_REG_STATUS:
    if ((value & NET64_STATUS_ONLINE) != 0u) {
      g_net64_regs.status |= NET64_STATUS_ONLINE;
    } else {
      g_net64_regs.status &= (uint32_t)~NET64_STATUS_ONLINE;
    }
    if ((value & NET64_STATUS_CONNECTED) != 0u) {
      g_net64_regs.status |= NET64_STATUS_CONNECTED;
    } else {
      g_net64_regs.status &= (uint32_t)~NET64_STATUS_CONNECTED;
    }
    break;
  case NET64_REG_LINK:
    g_net64_regs.link = value;
    break;
  default:
    break;
  }

  net64_refresh_status();
}

int net64_init(void) {
  if (g_net64_initialized) {
    return 0;
  }

  const net64_config_t *cfg64 = net64_config_get();
  if (net64_device_init(cfg64) != 0) {
    LOG_WARN("[NET64] Failed to initialize host backend.\n");
    return -1;
  }

  memset(&g_net64_regs, 0, sizeof(g_net64_regs));
  g_net64_regs.features = NET64_FEATURE_IPV6;
  if (cfg64->promisc) {
    g_net64_regs.features |= NET64_FEATURE_PROMISC;
  }
  if (cfg64->full_duplex) {
    g_net64_regs.features |= NET64_FEATURE_FULL_DUP;
  }
  g_net64_regs.features |= NET64_FEATURE_TAP_BACK;
  g_net64_regs.link = cfg64->link_speed_mbps;
  g_net64_regs.mtu = 1500;
  net64_device_set_debug_flags(cfg64->debug_flags);

  net64_update_mac_regs();
  net64_refresh_status();
  LOG_INFO("[NET64] Effective config: tap=%s mac=%02X:%02X:%02X:%02X:%02X:%02X promisc=%u queue=%u link=%uMbps duplex=%s dbg=0x%X\n",
           cfg64->tap_ifname,
           cfg64->mac[0], cfg64->mac[1], cfg64->mac[2],
           cfg64->mac[3], cfg64->mac[4], cfg64->mac[5],
           (unsigned int)cfg64->promisc,
           (unsigned int)cfg64->queue_depth,
           (unsigned int)cfg64->link_speed_mbps,
           cfg64->full_duplex ? "full" : "half",
           (unsigned int)cfg64->debug_flags);

  g_net64_initialized = 1;
  return 0;
}

void net64_shutdown(void) {
  if (!g_net64_initialized) {
    return;
  }

  net64_device_shutdown();
  memset(&g_net64_regs, 0, sizeof(g_net64_regs));
  g_net64_initialized = 0;
}

void net64_handle_reset(void) {
  if (!g_net64_initialized) {
    return;
  }
  net64_command_reset();
}

void net64_apply_runtime_config(void) {
  if (!g_net64_initialized) {
    return;
  }

  const net64_config_t *cfg64 = net64_config_get();
  if (cfg64 == NULL) {
    return;
  }

  net64_device_set_mac(cfg64->mac);
  net64_device_set_promisc(cfg64->promisc ? 1u : 0u);
  net64_device_set_debug_flags(cfg64->debug_flags);

  if (cfg64->promisc) {
    g_net64_regs.features |= NET64_FEATURE_PROMISC;
  } else {
    g_net64_regs.features &= ~NET64_FEATURE_PROMISC;
  }
  if (cfg64->full_duplex) {
    g_net64_regs.features |= NET64_FEATURE_FULL_DUP;
  } else {
    g_net64_regs.features &= ~NET64_FEATURE_FULL_DUP;
  }

  g_net64_regs.link = cfg64->link_speed_mbps;
  net64_update_mac_regs();
  net64_refresh_status();

  LOG_INFO("[NET64] Applied runtime config: mac=%02X:%02X:%02X:%02X:%02X:%02X promisc=%u link=%uMbps duplex=%s dbg=0x%X\n",
           cfg64->mac[0], cfg64->mac[1], cfg64->mac[2],
           cfg64->mac[3], cfg64->mac[4], cfg64->mac[5],
           (unsigned int)cfg64->promisc,
           (unsigned int)cfg64->link_speed_mbps,
           cfg64->full_duplex ? "full" : "half",
           (unsigned int)cfg64->debug_flags);
}

uint32_t handle_net64_read(uint32_t addr, uint8_t type) {
  if (!g_net64_initialized) {
    return 0;
  }

  uint32_t offset = addr & (NET64_REGSIZE - 1u);
  uint32_t width = net64_type_width(type);
  if (width == 0) {
    return 0;
  }

  if (type == OP_TYPE_LONGWORD) {
    return net64_read_register_u32(offset);
  }

  uint32_t aligned = offset & ~0x3u;
  uint32_t value32 = net64_read_register_u32(aligned);

  if (type == OP_TYPE_WORD) {
    if ((offset & 0x2u) != 0u) {
      return value32 & 0xFFFFu;
    }
    return (value32 >> 16) & 0xFFFFu;
  }

  uint32_t shift = (3u - (offset & 0x3u)) * 8u;
  return (value32 >> shift) & 0xFFu;
}

void handle_net64_write(uint32_t addr, uint32_t value, uint8_t type) {
  if (!g_net64_initialized) {
    return;
  }

  uint32_t offset = addr & (NET64_REGSIZE - 1u);
  uint32_t width = net64_type_width(type);
  if (width == 0) {
    return;
  }

  if (type == OP_TYPE_LONGWORD) {
    net64_write_register_u32(offset, value);
    return;
  }

  uint32_t aligned = offset & ~0x3u;
  uint32_t cur = net64_read_register_u32(aligned);
  uint32_t next = cur;

  if (type == OP_TYPE_WORD) {
    if ((offset & 0x2u) != 0u) {
      next = (cur & 0xFFFF0000u) | (value & 0xFFFFu);
    } else {
      next = (cur & 0x0000FFFFu) | ((value & 0xFFFFu) << 16);
    }
  } else {
    uint32_t shift = (3u - (offset & 0x3u)) * 8u;
    uint32_t mask = 0xFFu << shift;
    next = (cur & ~mask) | ((value & 0xFFu) << shift);
  }

  net64_write_register_u32(aligned, next);
}
