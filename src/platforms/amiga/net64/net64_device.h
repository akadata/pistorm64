// SPDX-License-Identifier: MIT
#ifndef PISTORM_AMIGA_NET64_DEVICE_H
#define PISTORM_AMIGA_NET64_DEVICE_H

#include <stdint.h>

#include "net64_config.h"

#define NET64_MAX_FRAME 2048

typedef struct net64_stats {
  uint64_t tx_packets;
  uint64_t tx_bytes;
  uint64_t tx_errors;
  uint64_t rx_packets;
  uint64_t rx_bytes;
  uint64_t rx_dropped;
} net64_stats_t;

int net64_device_init(const net64_config_t *cfg);
void net64_device_shutdown(void);
void net64_device_reset_queues(void);

int net64_device_send_frame(const uint8_t *frame, uint16_t len);
int net64_device_recv_frame(uint8_t *dst, uint16_t dst_len, uint16_t *out_len);
uint32_t net64_device_rx_pending(void);

int net64_device_link_up(void);
void net64_device_get_mac(uint8_t out_mac[6]);
void net64_device_set_mac(const uint8_t mac[6]);
void net64_device_set_promisc(uint8_t enabled);
void net64_device_set_debug_flags(uint32_t flags);

void net64_device_get_stats(net64_stats_t *stats);

#endif
