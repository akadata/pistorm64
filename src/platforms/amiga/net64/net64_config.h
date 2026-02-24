// SPDX-License-Identifier: MIT
#ifndef PISTORM_AMIGA_NET64_CONFIG_H
#define PISTORM_AMIGA_NET64_CONFIG_H

#include <stdint.h>
#include <net/if.h>

enum {
  NET64_DBG_TX    = 1u << 0,
  NET64_DBG_RX    = 1u << 1,
  NET64_DBG_REGS  = 1u << 2,
  NET64_DBG_CFG   = 1u << 3,
  NET64_DBG_STATS = 1u << 4,
  NET64_DBG_ALL   = 0xFFFFFFFFu
};

typedef struct net64_config {
  char tap_ifname[IFNAMSIZ];
  uint8_t mac[6];
  uint8_t mac_overridden;
  uint8_t promisc;
  uint16_t queue_depth;
  uint32_t link_speed_mbps;
  uint8_t full_duplex;
  uint32_t debug_flags;
} net64_config_t;

void net64_config_init_once(void);
int net64_config_setvar(const char *var, const char *val);
const net64_config_t *net64_config_get(void);

#endif
