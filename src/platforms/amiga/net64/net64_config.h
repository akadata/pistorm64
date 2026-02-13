// SPDX-License-Identifier: MIT
#ifndef PISTORM_AMIGA_NET64_CONFIG_H
#define PISTORM_AMIGA_NET64_CONFIG_H

#include <stdint.h>
#include <net/if.h>

typedef struct net64_config {
  char tap_ifname[IFNAMSIZ];
  uint8_t mac[6];
  uint8_t mac_overridden;
  uint8_t promisc;
  uint16_t queue_depth;
  uint32_t link_speed_mbps;
  uint8_t full_duplex;
} net64_config_t;

void net64_config_init_once(void);
int net64_config_setvar(const char *var, const char *val);
const net64_config_t *net64_config_get(void);

#endif
