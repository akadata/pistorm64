// SPDX-License-Identifier: MIT
#ifndef PISTORM_AMIGA_NET64_BUS_H
#define PISTORM_AMIGA_NET64_BUS_H

#include <stdint.h>

#define NET64_REGSIZE 0x00010000u

#define NET64_REG_CMD        0x0000u
#define NET64_REG_STATUS     0x0004u
#define NET64_REG_TX_ADDR    0x0008u
#define NET64_REG_TX_LEN     0x000Cu
#define NET64_REG_RX_ADDR    0x0010u
#define NET64_REG_RX_LEN     0x0014u
#define NET64_REG_RX_ACTUAL  0x0018u
#define NET64_REG_IRQ_STATUS 0x001Cu
#define NET64_REG_IRQ_MASK   0x0020u
#define NET64_REG_MAC_LO     0x0024u
#define NET64_REG_MAC_HI     0x0028u
#define NET64_REG_FEATURES   0x002Cu
#define NET64_REG_LINK       0x0030u
#define NET64_REG_MTU        0x0034u
#define NET64_REG_RX_PENDING 0x0038u

#define NET64_REG_TX_PKTS_LO 0x0040u
#define NET64_REG_TX_PKTS_HI 0x0044u
#define NET64_REG_TX_BYTES_LO 0x0048u
#define NET64_REG_TX_BYTES_HI 0x004Cu
#define NET64_REG_TX_ERRS_LO 0x0050u
#define NET64_REG_TX_ERRS_HI 0x0054u
#define NET64_REG_RX_PKTS_LO 0x0058u
#define NET64_REG_RX_PKTS_HI 0x005Cu
#define NET64_REG_RX_BYTES_LO 0x0060u
#define NET64_REG_RX_BYTES_HI 0x0064u
#define NET64_REG_RX_DROP_LO 0x0068u
#define NET64_REG_RX_DROP_HI 0x006Cu

#define NET64_CMD_NOP       0u
#define NET64_CMD_RESET     1u
#define NET64_CMD_TX_KICK   2u
#define NET64_CMD_RX_POP    3u
#define NET64_CMD_APPLY_CFG 4u

#define NET64_STATUS_READY      (1u << 0)
#define NET64_STATUS_LINK_UP    (1u << 1)
#define NET64_STATUS_RX_PENDING (1u << 2)
#define NET64_STATUS_TX_OK      (1u << 3)
#define NET64_STATUS_TX_ERR     (1u << 4)
#define NET64_STATUS_ONLINE     (1u << 5)
#define NET64_STATUS_CONNECTED  (1u << 6)

#define NET64_IRQ_RX_EVENT      (1u << 0)
#define NET64_IRQ_TX_EVENT      (1u << 1)
#define NET64_IRQ_LINK_EVENT    (1u << 2)

#define NET64_FEATURE_PROMISC   (1u << 0)
#define NET64_FEATURE_IPV6      (1u << 1)
#define NET64_FEATURE_FULL_DUP  (1u << 2)
#define NET64_FEATURE_TAP_BACK  (1u << 3)

int net64_init(void);
void net64_shutdown(void);
void net64_handle_reset(void);

uint32_t handle_net64_read(uint32_t addr, uint8_t type);
void handle_net64_write(uint32_t addr, uint32_t value, uint8_t type);

#endif
