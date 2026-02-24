// SPDX-License-Identifier: MIT

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "net64_config.h"
#include "log.h"

static net64_config_t g_cfg;
static int g_cfg_initialized;

static int net64_parse_bool(const char *val, int default_on) {
  if (val == NULL || val[0] == '\0') {
    return default_on;
  }
  if (strcasecmp(val, "1") == 0 || strcasecmp(val, "true") == 0 ||
      strcasecmp(val, "yes") == 0 || strcasecmp(val, "on") == 0) {
    return 1;
  }
  if (strcasecmp(val, "0") == 0 || strcasecmp(val, "false") == 0 ||
      strcasecmp(val, "no") == 0 || strcasecmp(val, "off") == 0) {
    return 0;
  }
  return -1;
}

static int net64_parse_mac(const char *val, uint8_t out[6]) {
  unsigned int b[6];
  if (val == NULL) {
    return 0;
  }
  if (sscanf(val, "%2x:%2x:%2x:%2x:%2x:%2x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
    return 0;
  }
  for (int i = 0; i < 6; i++) {
    out[i] = (uint8_t)(b[i] & 0xFFu);
  }
  if ((out[0] & 0x01u) != 0u) {
    return 0;
  }
  return 1;
}

static int net64_parse_debug_flags(const char *val, uint32_t *out_flags) {
  if (out_flags == NULL) {
    return 0;
  }
  if (val == NULL || val[0] == '\0') {
    *out_flags = NET64_DBG_TX | NET64_DBG_RX | NET64_DBG_CFG;
    return 1;
  }

  if (strcasecmp(val, "off") == 0 || strcmp(val, "0") == 0 ||
      strcasecmp(val, "false") == 0 || strcasecmp(val, "no") == 0) {
    *out_flags = 0;
    return 1;
  }
  if (strcasecmp(val, "on") == 0 || strcmp(val, "1") == 0 ||
      strcasecmp(val, "true") == 0 || strcasecmp(val, "yes") == 0 ||
      strcasecmp(val, "packets") == 0) {
    *out_flags = NET64_DBG_TX | NET64_DBG_RX | NET64_DBG_CFG;
    return 1;
  }
  if (strcasecmp(val, "all") == 0) {
    *out_flags = NET64_DBG_ALL;
    return 1;
  }

  char buf[128];
  snprintf(buf, sizeof(buf), "%s", val);
  uint32_t flags = 0;
  char *save = NULL;
  for (char *tok = strtok_r(buf, ",| ", &save); tok != NULL; tok = strtok_r(NULL, ",| ", &save)) {
    if (strcasecmp(tok, "tx") == 0) {
      flags |= NET64_DBG_TX;
    } else if (strcasecmp(tok, "rx") == 0) {
      flags |= NET64_DBG_RX;
    } else if (strcasecmp(tok, "cfg") == 0) {
      flags |= NET64_DBG_CFG;
    } else if (strcasecmp(tok, "regs") == 0 || strcasecmp(tok, "reg") == 0) {
      flags |= NET64_DBG_REGS;
    } else if (strcasecmp(tok, "stats") == 0) {
      flags |= NET64_DBG_STATS;
    } else if (strcasecmp(tok, "packets") == 0) {
      flags |= NET64_DBG_TX | NET64_DBG_RX;
    } else if (strcasecmp(tok, "none") == 0 || strcasecmp(tok, "off") == 0) {
      /* no-op */
    } else {
      return 0;
    }
  }
  *out_flags = flags;
  return 1;
}

static uint32_t net64_hash32(uint32_t hash, uint8_t c) {
  hash ^= c;
  hash *= 16777619u;
  return hash;
}

static int net64_read_first_line(const char *path, char *out, size_t out_len) {
  FILE *in = fopen(path, "rb");
  if (in == NULL) {
    return 0;
  }
  if (fgets(out, (int)out_len, in) == NULL) {
    fclose(in);
    return 0;
  }
  fclose(in);
  size_t n = strlen(out);
  while (n > 0) {
    if (out[n - 1] == '\n' || out[n - 1] == '\r') {
      out[n - 1] = '\0';
      n--;
    } else {
      break;
    }
  }
  return 1;
}

static void net64_build_default_mac(uint8_t mac[6]) {
  char buf[256];
  uint32_t hash = 2166136261u;

  if (net64_read_first_line("/proc/device-tree/serial-number", buf, sizeof(buf))) {
    for (size_t i = 0; buf[i] != '\0'; i++) {
      hash = net64_hash32(hash, (uint8_t)buf[i]);
    }
  }

  if (net64_read_first_line("/etc/machine-id", buf, sizeof(buf))) {
    for (size_t i = 0; buf[i] != '\0'; i++) {
      hash = net64_hash32(hash, (uint8_t)buf[i]);
    }
  }

  if (gethostname(buf, sizeof(buf)) == 0) {
    for (size_t i = 0; i < sizeof(buf) && buf[i] != '\0'; i++) {
      hash = net64_hash32(hash, (uint8_t)buf[i]);
    }
  }

  hash ^= (uint32_t)getpid();
  hash ^= (uint32_t)time(NULL);

  mac[0] = 0x02u;
  mac[1] = 0x50u;
  mac[2] = (uint8_t)((hash >> 24) & 0xFFu);
  mac[3] = (uint8_t)((hash >> 16) & 0xFFu);
  mac[4] = (uint8_t)((hash >> 8) & 0xFFu);
  mac[5] = (uint8_t)(hash & 0xFFu);
}

void net64_config_init_once(void) {
  if (g_cfg_initialized) {
    return;
  }

  memset(&g_cfg, 0, sizeof(g_cfg));
  snprintf(g_cfg.tap_ifname, sizeof(g_cfg.tap_ifname), "tap0");
  net64_build_default_mac(g_cfg.mac);
  g_cfg.mac_overridden = 0;
  g_cfg.promisc = 0;
  g_cfg.queue_depth = 128;
  g_cfg.link_speed_mbps = 1000;
  g_cfg.full_duplex = 1;
  g_cfg.debug_flags = 0;
  g_cfg_initialized = 1;

  LOG_INFO("[NET64] Defaults: tap=%s mac=%02X:%02X:%02X:%02X:%02X:%02X queue=%u promisc=%u\n",
           g_cfg.tap_ifname,
           g_cfg.mac[0], g_cfg.mac[1], g_cfg.mac[2],
           g_cfg.mac[3], g_cfg.mac[4], g_cfg.mac[5],
           (unsigned int)g_cfg.queue_depth,
           (unsigned int)g_cfg.promisc);
}

const net64_config_t *net64_config_get(void) {
  net64_config_init_once();
  return &g_cfg;
}

int net64_config_setvar(const char *var, const char *val) {
  net64_config_init_once();

  if (var == NULL) {
    return 0;
  }

  if (strcmp(var, "net64_tap") == 0) {
    if (val != NULL && val[0] != '\0') {
      snprintf(g_cfg.tap_ifname, sizeof(g_cfg.tap_ifname), "%s", val);
      LOG_INFO("[NET64] tap interface set to %s\n", g_cfg.tap_ifname);
    }
    return 1;
  }

  if (strcmp(var, "net64_mac") == 0) {
    uint8_t mac[6];
    if (!net64_parse_mac(val, mac)) {
      LOG_WARN("[NET64] Invalid net64_mac value '%s' (expected xx:xx:xx:xx:xx:xx)\n",
               val ? val : "(null)");
      return 1;
    }
    memcpy(g_cfg.mac, mac, sizeof(g_cfg.mac));
    g_cfg.mac_overridden = 1;
    LOG_INFO("[NET64] MAC override %02X:%02X:%02X:%02X:%02X:%02X\n",
             g_cfg.mac[0], g_cfg.mac[1], g_cfg.mac[2],
             g_cfg.mac[3], g_cfg.mac[4], g_cfg.mac[5]);
    return 1;
  }

  if (strcmp(var, "net64_promisc") == 0) {
    int parsed = net64_parse_bool(val, 1);
    if (parsed < 0) {
      LOG_WARN("[NET64] Invalid net64_promisc value '%s'\n", val ? val : "(null)");
      return 1;
    }
    g_cfg.promisc = (uint8_t)parsed;
    LOG_INFO("[NET64] Promiscuous mode %s\n", g_cfg.promisc ? "enabled" : "disabled");
    return 1;
  }

  if (strcmp(var, "net64_queue_depth") == 0) {
    if (val != NULL && val[0] != '\0') {
      long depth = strtol(val, NULL, 10);
      if (depth < 8) {
        depth = 8;
      }
      if (depth > 1024) {
        depth = 1024;
      }
      g_cfg.queue_depth = (uint16_t)depth;
      LOG_INFO("[NET64] Queue depth set to %u\n", (unsigned int)g_cfg.queue_depth);
    }
    return 1;
  }

  if (strcmp(var, "net64_link_mbps") == 0) {
    if (val != NULL && val[0] != '\0') {
      long speed = strtol(val, NULL, 10);
      if (speed < 10) {
        speed = 10;
      }
      if (speed > 100000) {
        speed = 100000;
      }
      g_cfg.link_speed_mbps = (uint32_t)speed;
      LOG_INFO("[NET64] Link speed set to %u Mbps\n", (unsigned int)g_cfg.link_speed_mbps);
    }
    return 1;
  }

  if (strcmp(var, "net64_duplex") == 0) {
    if (val == NULL || val[0] == '\0' || strcasecmp(val, "full") == 0) {
      g_cfg.full_duplex = 1;
    } else if (strcasecmp(val, "half") == 0) {
      g_cfg.full_duplex = 0;
    }
    LOG_INFO("[NET64] Duplex set to %s\n", g_cfg.full_duplex ? "full" : "half");
    return 1;
  }

  if (strcmp(var, "net64_debug") == 0) {
    uint32_t flags = 0;
    if (!net64_parse_debug_flags(val, &flags)) {
      LOG_WARN("[NET64] Invalid net64_debug value '%s' (use off|on|all|tx,rx,cfg,regs,stats)\n",
               val ? val : "(null)");
      return 1;
    }
    g_cfg.debug_flags = flags;
    LOG_INFO("[NET64] Debug flags set to 0x%X\n", (unsigned int)g_cfg.debug_flags);
    return 1;
  }

  if (strcmp(var, "net64") == 0) {
    if (val != NULL && val[0] != '\0') {
      if (strchr(val, ':') != NULL) {
        uint8_t mac[6];
        if (net64_parse_mac(val, mac)) {
          memcpy(g_cfg.mac, mac, sizeof(g_cfg.mac));
          g_cfg.mac_overridden = 1;
          return 1;
        }
      }
      if (isalnum((unsigned char)val[0])) {
        snprintf(g_cfg.tap_ifname, sizeof(g_cfg.tap_ifname), "%s", val);
      }
    }
    return 1;
  }

  return 0;
}
