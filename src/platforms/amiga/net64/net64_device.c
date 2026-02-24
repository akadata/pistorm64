// SPDX-License-Identifier: MIT

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <linux/if_tun.h>
#include <net/if.h>

#include "log.h"
#include "net64_device.h"

typedef struct net64_rx_ring {
  uint8_t **frames;
  uint16_t *lengths;
  uint16_t depth;
  uint16_t head;
  uint16_t tail;
  uint16_t count;
} net64_rx_ring_t;

typedef struct net64_state {
  int initialized;
  int tap_fd;
  int tap_live;
  char tap_name[IFNAMSIZ];

  uint8_t mac[6];
  uint8_t promisc;
  uint32_t debug_flags;
  uint8_t promisc_ctl_supported;
  uint8_t promisc_warned;

  pthread_t rx_thread;
  int rx_thread_running;

  pthread_mutex_t lock;
  net64_rx_ring_t rx_ring;
  net64_stats_t stats;
} net64_state_t;

static net64_state_t g_net64;

static int net64_dbg_enabled(uint32_t flag) {
  if (log_get_level() < LOG_LEVEL_DEBUG) {
    return 0;
  }
  return (g_net64.debug_flags & flag) != 0u;
}

static void net64_log_frame(const char *tag, const uint8_t *frame, uint16_t len) {
  if (frame == NULL || len < 14) {
    return;
  }
  uint16_t ethertype = (uint16_t)(((uint16_t)frame[12] << 8) | frame[13]);
  LOG_DEBUG("[NET64] %s len=%u eth=0x%04X dst=%02X:%02X:%02X:%02X:%02X:%02X src=%02X:%02X:%02X:%02X:%02X:%02X\n",
            tag, (unsigned int)len, (unsigned int)ethertype,
            frame[0], frame[1], frame[2], frame[3], frame[4], frame[5],
            frame[6], frame[7], frame[8], frame[9], frame[10], frame[11]);
}

static void net64_rx_ring_clear_locked(net64_rx_ring_t *ring) {
  ring->head = 0;
  ring->tail = 0;
  ring->count = 0;
}

static int net64_rx_ring_init(net64_rx_ring_t *ring, uint16_t depth) {
  memset(ring, 0, sizeof(*ring));
  ring->depth = depth;
  ring->frames = (uint8_t **)calloc(depth, sizeof(ring->frames[0]));
  ring->lengths = (uint16_t *)calloc(depth, sizeof(ring->lengths[0]));
  if (ring->frames == NULL || ring->lengths == NULL) {
    free(ring->frames);
    free(ring->lengths);
    memset(ring, 0, sizeof(*ring));
    return -1;
  }

  for (uint16_t i = 0; i < depth; i++) {
    ring->frames[i] = (uint8_t *)malloc(NET64_MAX_FRAME);
    if (ring->frames[i] == NULL) {
      for (uint16_t j = 0; j < i; j++) {
        free(ring->frames[j]);
      }
      free(ring->frames);
      free(ring->lengths);
      memset(ring, 0, sizeof(*ring));
      return -1;
    }
  }

  return 0;
}

static void net64_rx_ring_free(net64_rx_ring_t *ring) {
  if (ring->frames != NULL) {
    for (uint16_t i = 0; i < ring->depth; i++) {
      free(ring->frames[i]);
    }
  }
  free(ring->frames);
  free(ring->lengths);
  memset(ring, 0, sizeof(*ring));
}

static int net64_rx_ring_push_locked(net64_rx_ring_t *ring, const uint8_t *frame, uint16_t len) {
  if (ring->count >= ring->depth) {
    return -1;
  }

  uint16_t slot = ring->head;
  memcpy(ring->frames[slot], frame, len);
  ring->lengths[slot] = len;

  ring->head = (uint16_t)((ring->head + 1u) % ring->depth);
  ring->count++;
  return 0;
}

static int net64_rx_ring_pop_locked(net64_rx_ring_t *ring, uint8_t *dst, uint16_t dst_len,
                                    uint16_t *out_len) {
  if (ring->count == 0) {
    if (out_len != NULL) {
      *out_len = 0;
    }
    return -1;
  }

  uint16_t slot = ring->tail;
  uint16_t len = ring->lengths[slot];
  uint16_t copy_len = len;
  if (copy_len > dst_len) {
    copy_len = dst_len;
  }

  memcpy(dst, ring->frames[slot], copy_len);

  ring->tail = (uint16_t)((ring->tail + 1u) % ring->depth);
  ring->count--;

  if (out_len != NULL) {
    *out_len = len;
  }
  return 0;
}

static int net64_apply_promisc(const char *ifname, uint8_t enabled) {
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    return -1;
  }

  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", ifname);

  if (ioctl(sock, SIOCGIFFLAGS, &ifr) != 0) {
    close(sock);
    return -1;
  }

  if (enabled) {
    ifr.ifr_flags |= IFF_PROMISC;
  } else {
    ifr.ifr_flags &= (short)~IFF_PROMISC;
  }

  if (ioctl(sock, SIOCSIFFLAGS, &ifr) != 0) {
    if (errno == EPERM || errno == EACCES) {
      close(sock);
      return -2;
    }
    close(sock);
    return -1;
  }

  close(sock);
  return 0;
}

static int net64_open_tap(const char *tap_name, char actual_name[IFNAMSIZ]) {
  int fd = open("/dev/net/tun", O_RDWR | O_NONBLOCK);
  if (fd < 0) {
    LOG_WARN("[NET64] open(/dev/net/tun) failed: %s\n", strerror(errno));
    return -1;
  }

  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
  if (tap_name != NULL && tap_name[0] != '\0') {
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", tap_name);
  }

  if (ioctl(fd, TUNSETIFF, &ifr) != 0) {
    LOG_WARN("[NET64] TUNSETIFF failed for '%s': %s\n",
             tap_name ? tap_name : "(auto)", strerror(errno));
    close(fd);
    return -1;
  }

  snprintf(actual_name, IFNAMSIZ, "%s", ifr.ifr_name);
  return fd;
}

static void *net64_rx_thread_main(void *arg) {
  (void)arg;

  uint8_t frame[NET64_MAX_FRAME];
  while (g_net64.rx_thread_running) {
    if (g_net64.tap_fd < 0) {
      usleep(10000);
      continue;
    }

    struct pollfd pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = g_net64.tap_fd;
    pfd.events = POLLIN;

    int pr = poll(&pfd, 1, 100);
    if (pr <= 0) {
      continue;
    }
    if ((pfd.revents & POLLIN) == 0) {
      continue;
    }

    ssize_t rd = read(g_net64.tap_fd, frame, sizeof(frame));
    if (rd <= 0) {
      continue;
    }

    uint16_t len = (uint16_t)rd;
    if (len > NET64_MAX_FRAME) {
      len = NET64_MAX_FRAME;
    }

    pthread_mutex_lock(&g_net64.lock);
    if (net64_rx_ring_push_locked(&g_net64.rx_ring, frame, len) != 0) {
      g_net64.stats.rx_dropped++;
      if (net64_dbg_enabled(NET64_DBG_RX)) {
        LOG_DEBUG("[NET64] RX drop len=%u (queue full)\n", (unsigned int)len);
      }
    } else {
      g_net64.stats.rx_packets++;
      g_net64.stats.rx_bytes += len;
      if (net64_dbg_enabled(NET64_DBG_RX)) {
        net64_log_frame("RX<-TAP", frame, len);
      }
    }
    pthread_mutex_unlock(&g_net64.lock);
  }

  return NULL;
}

int net64_device_init(const net64_config_t *cfg) {
  if (g_net64.initialized) {
    return 0;
  }

  memset(&g_net64, 0, sizeof(g_net64));

  if (pthread_mutex_init(&g_net64.lock, NULL) != 0) {
    LOG_WARN("[NET64] Failed to initialize mutex.\n");
    return -1;
  }

  uint16_t queue_depth = (cfg != NULL) ? cfg->queue_depth : 128u;
  if (queue_depth < 8) {
    queue_depth = 8;
  }
  if (net64_rx_ring_init(&g_net64.rx_ring, queue_depth) != 0) {
    LOG_WARN("[NET64] Failed to allocate RX ring depth=%u.\n", (unsigned int)queue_depth);
    pthread_mutex_destroy(&g_net64.lock);
    return -1;
  }

  if (cfg != NULL) {
    memcpy(g_net64.mac, cfg->mac, sizeof(g_net64.mac));
    g_net64.promisc = cfg->promisc;
    g_net64.debug_flags = cfg->debug_flags;
  } else {
    g_net64.mac[0] = 0x02;
    g_net64.mac[1] = 0x50;
    g_net64.mac[2] = 0x00;
    g_net64.mac[3] = 0x00;
    g_net64.mac[4] = 0x00;
    g_net64.mac[5] = 0x64;
    g_net64.promisc = 0;
    g_net64.debug_flags = 0;
  }
  g_net64.promisc_ctl_supported = 1;
  g_net64.promisc_warned = 0;

  g_net64.tap_fd = -1;
  g_net64.tap_live = 0;
  g_net64.tap_name[0] = '\0';

  const char *tap_name = (cfg != NULL) ? cfg->tap_ifname : "tap0";
  g_net64.tap_fd = net64_open_tap(tap_name, g_net64.tap_name);
  if (g_net64.tap_fd >= 0) {
    g_net64.tap_live = 1;
    int prc = net64_apply_promisc(g_net64.tap_name, g_net64.promisc);
    if (prc == -2) {
      g_net64.promisc_ctl_supported = 0;
      g_net64.promisc_warned = 1;
      LOG_WARN("[NET64] No permission to control promisc on %s; continuing without promisc ioctl.\n",
               g_net64.tap_name);
    } else if (prc != 0) {
      LOG_WARN("[NET64] Unable to set promisc=%u on %s (continuing).\n",
               (unsigned int)g_net64.promisc, g_net64.tap_name);
    }
  } else {
    LOG_WARN("[NET64] TAP backend unavailable, using loopback mode for test plumbing.\n");
  }

  g_net64.rx_thread_running = 1;
  if (pthread_create(&g_net64.rx_thread, NULL, net64_rx_thread_main, NULL) != 0) {
    LOG_WARN("[NET64] Failed to start RX thread.\n");
    g_net64.rx_thread_running = 0;
    if (g_net64.tap_fd >= 0) {
      close(g_net64.tap_fd);
      g_net64.tap_fd = -1;
    }
    net64_rx_ring_free(&g_net64.rx_ring);
    pthread_mutex_destroy(&g_net64.lock);
    return -1;
  }

  g_net64.initialized = 1;

  LOG_INFO("[NET64] Ready backend=%s if=%s queue=%u mac=%02X:%02X:%02X:%02X:%02X:%02X\n",
           g_net64.tap_live ? "tap" : "loopback",
           g_net64.tap_live ? g_net64.tap_name : "(none)",
           (unsigned int)g_net64.rx_ring.depth,
           g_net64.mac[0], g_net64.mac[1], g_net64.mac[2],
           g_net64.mac[3], g_net64.mac[4], g_net64.mac[5]);

  return 0;
}

void net64_device_shutdown(void) {
  if (!g_net64.initialized) {
    return;
  }

  g_net64.rx_thread_running = 0;
  pthread_join(g_net64.rx_thread, NULL);

  if (g_net64.tap_fd >= 0) {
    close(g_net64.tap_fd);
    g_net64.tap_fd = -1;
  }

  pthread_mutex_lock(&g_net64.lock);
  net64_rx_ring_free(&g_net64.rx_ring);
  pthread_mutex_unlock(&g_net64.lock);

  pthread_mutex_destroy(&g_net64.lock);
  memset(&g_net64, 0, sizeof(g_net64));
}

void net64_device_reset_queues(void) {
  if (!g_net64.initialized) {
    return;
  }
  pthread_mutex_lock(&g_net64.lock);
  net64_rx_ring_clear_locked(&g_net64.rx_ring);
  pthread_mutex_unlock(&g_net64.lock);
}

int net64_device_send_frame(const uint8_t *frame, uint16_t len) {
  if (!g_net64.initialized || frame == NULL || len == 0 || len > NET64_MAX_FRAME) {
    return -1;
  }

  pthread_mutex_lock(&g_net64.lock);

  int rc = 0;
  if (g_net64.tap_live && g_net64.tap_fd >= 0) {
    ssize_t wr = write(g_net64.tap_fd, frame, len);
    if (wr != (ssize_t)len) {
      rc = -1;
      g_net64.stats.tx_errors++;
      if (net64_dbg_enabled(NET64_DBG_TX)) {
        LOG_DEBUG("[NET64] TX write failed len=%u rc=%zd errno=%d\n",
                  (unsigned int)len, wr, errno);
      }
    }
  } else {
    if (net64_rx_ring_push_locked(&g_net64.rx_ring, frame, len) != 0) {
      rc = -1;
      g_net64.stats.rx_dropped++;
    } else {
      g_net64.stats.rx_packets++;
      g_net64.stats.rx_bytes += len;
    }
  }

  if (rc == 0) {
    g_net64.stats.tx_packets++;
    g_net64.stats.tx_bytes += len;
    if (net64_dbg_enabled(NET64_DBG_TX)) {
      net64_log_frame("TX", frame, len);
    }
  }

  pthread_mutex_unlock(&g_net64.lock);
  return rc;
}

int net64_device_recv_frame(uint8_t *dst, uint16_t dst_len, uint16_t *out_len) {
  if (!g_net64.initialized || dst == NULL || dst_len == 0) {
    if (out_len != NULL) {
      *out_len = 0;
    }
    return -1;
  }

  pthread_mutex_lock(&g_net64.lock);
  int rc = net64_rx_ring_pop_locked(&g_net64.rx_ring, dst, dst_len, out_len);
  if (rc == 0 && net64_dbg_enabled(NET64_DBG_RX) && out_len != NULL) {
    net64_log_frame("RX->AMIGA", dst, *out_len);
  }
  pthread_mutex_unlock(&g_net64.lock);
  return rc;
}

uint32_t net64_device_rx_pending(void) {
  if (!g_net64.initialized) {
    return 0;
  }

  pthread_mutex_lock(&g_net64.lock);
  uint32_t count = (uint32_t)g_net64.rx_ring.count;
  pthread_mutex_unlock(&g_net64.lock);
  return count;
}

int net64_device_link_up(void) {
  if (!g_net64.initialized) {
    return 0;
  }
  return 1;
}

void net64_device_get_mac(uint8_t out_mac[6]) {
  if (out_mac == NULL) {
    return;
  }

  if (!g_net64.initialized) {
    memset(out_mac, 0, 6);
    return;
  }

  pthread_mutex_lock(&g_net64.lock);
  memcpy(out_mac, g_net64.mac, 6);
  pthread_mutex_unlock(&g_net64.lock);
}

void net64_device_set_mac(const uint8_t mac[6]) {
  if (!g_net64.initialized || mac == NULL) {
    return;
  }

  pthread_mutex_lock(&g_net64.lock);
  memcpy(g_net64.mac, mac, 6);
  pthread_mutex_unlock(&g_net64.lock);
}

void net64_device_set_promisc(uint8_t enabled) {
  if (!g_net64.initialized) {
    return;
  }

  pthread_mutex_lock(&g_net64.lock);
  g_net64.promisc = enabled ? 1u : 0u;
  char ifname[IFNAMSIZ];
  snprintf(ifname, sizeof(ifname), "%s", g_net64.tap_name);
  int tap_live = g_net64.tap_live;
  pthread_mutex_unlock(&g_net64.lock);

  if (tap_live) {
    if (g_net64.promisc_ctl_supported == 0) {
      return;
    }
    int prc = net64_apply_promisc(ifname, g_net64.promisc);
    if (prc == -2) {
      g_net64.promisc_ctl_supported = 0;
      if (!g_net64.promisc_warned) {
        LOG_WARN("[NET64] No permission to control promisc on %s; suppressing further promisc warnings.\n",
                 ifname);
        g_net64.promisc_warned = 1;
      }
    } else if (prc != 0) {
      LOG_WARN("[NET64] Failed to set promisc=%u on %s\n", (unsigned int)g_net64.promisc, ifname);
    } else if (net64_dbg_enabled(NET64_DBG_CFG)) {
      LOG_DEBUG("[NET64] promisc=%u applied to %s\n", (unsigned int)g_net64.promisc, ifname);
    }
  }
}

void net64_device_set_debug_flags(uint32_t flags) {
  if (!g_net64.initialized) {
    return;
  }
  pthread_mutex_lock(&g_net64.lock);
  g_net64.debug_flags = flags;
  pthread_mutex_unlock(&g_net64.lock);
}

void net64_device_get_stats(net64_stats_t *stats) {
  if (stats == NULL) {
    return;
  }

  memset(stats, 0, sizeof(*stats));
  if (!g_net64.initialized) {
    return;
  }

  pthread_mutex_lock(&g_net64.lock);
  *stats = g_net64.stats;
  pthread_mutex_unlock(&g_net64.lock);
}
