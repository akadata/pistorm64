// SPDX-License-Identifier: MIT

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "src/gpio/ps_protocol.h"

static void print_usage(const char* progname) {
  printf("Usage: %s [OPTIONS] [SAMPLES]\n", progname);
  printf("Options:\n");
  printf("  -c, --config CFG  Config file used for pistorm/userspace GPCLK/strobe selection (default: default.cfg)\n");
  printf("  -h, --help        Show this help and exit\n");
}

static void preparse_pistorm_local(const char* filename) {
  FILE* in = NULL;
  char line[256];

  if (!filename || !filename[0]) {
    return;
  }

  in = fopen(filename, "rb");
  if (!in) {
    return;
  }

  while (fgets(line, sizeof(line), in)) {
    char key[64];
    char value[64];

    if (line[0] == '#' || line[0] == '/') {
      continue;
    }

    if (sscanf(line, " %63s %63s", key, value) != 2) {
      continue;
    }

    if (strcasecmp(key, "pistorm") == 0) {
      (void)ps_select_backend(value);
      continue;
    }
    if (strcasecmp(key, "pistorm-gpclk-src") == 0) {
      (void)ps_set_userspace_gpclk_src((uint32_t)strtoul(value, NULL, 0));
      continue;
    }
    if (strcasecmp(key, "pistorm-gpclk-div") == 0) {
      (void)ps_set_userspace_gpclk_div((uint32_t)strtoul(value, NULL, 0));
      continue;
    }
    if (strcasecmp(key, "pistorm-mmio-wr-stretch") == 0) {
      (void)ps_set_userspace_wr_stretch((uint32_t)strtoul(value, NULL, 0));
      continue;
    }
    if (strcasecmp(key, "pistorm-mmio-rd-stretch") == 0) {
      (void)ps_set_userspace_rd_stretch((uint32_t)strtoul(value, NULL, 0));
      continue;
    }
  }

  fclose(in);
}

static bool seen(const uint16_t* vals, size_t count, uint16_t v) {
  for (size_t i = 0; i < count; i++) {
    if (vals[i] == v) {
      return true;
    }
  }
  return false;
}

int main(int argc, char** argv) {
  const char* cfg_for_backend = "default.cfg";
  const char* progname = (argc > 0 && argv[0] && argv[0][0]) ? argv[0] : "pistorm_truth_test";
  int samples = 1000;
  int rc = 0;
  uint16_t vmin = 0xFFFFu;
  uint16_t vmax = 0x0000u;
  uint16_t distinct[6000];
  size_t distinct_cnt = 0;
  const char* samples_arg = NULL;

  for (int i = 1; i < argc; i++) {
    const char* arg = argv[i];

    if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
      print_usage(progname);
      return 0;
    }

    if (strcmp(arg, "-c") == 0 || strcmp(arg, "--config") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "%s: %s requires a path argument\n", progname, arg);
        return 1;
      }
      cfg_for_backend = argv[++i];
      continue;
    }

    if (arg[0] == '-' && arg[1] != '\0') {
      fprintf(stderr, "%s: unknown option '%s'\n", progname, arg);
      print_usage(progname);
      return 1;
    }

    if (samples_arg) {
      fprintf(stderr, "%s: too many arguments\n", progname);
      print_usage(progname);
      return 1;
    }
    samples_arg = arg;
  }

  if (samples_arg) {
    samples = atoi(samples_arg);
    if (samples < 1) {
      samples = 500;
    }
    if (samples > 5000) {
      samples = 5000;
    }
  }

  preparse_pistorm_local(cfg_for_backend);
  printf("[TRUTH] Selected backend (pre-init): %s\n", ps_get_backend());
  if ((strcasecmp(ps_get_backend(), "kmod") == 0 || strcasecmp(ps_get_backend(), "kernel") == 0) &&
      access("/dev/pistorm", R_OK | W_OK) != 0) {
    fprintf(stderr, "[TRUTH] kernel backend selected but /dev/pistorm is unavailable.\n");
    return 1;
  }

  ps_setup_protocol();
  ps_reset_state_machine();
  ps_pulse_reset();
  usleep(1500u);

  for (int i = 0; i < samples; i++) {
    uint16_t v = ps_read_16(0x00DFF006u);

    if (v < vmin) {
      vmin = v;
    }
    if (v > vmax) {
      vmax = v;
    }
    if (!seen(distinct, distinct_cnt, v) && distinct_cnt < 6000) {
      distinct[distinct_cnt++] = v;
    }
  }

  printf("VHPOSR samples=%d distinct=%zu min=0x%04x max=0x%04x\n", samples, distinct_cnt, vmin, vmax);
  if (distinct_cnt <= 1) {
    fprintf(stderr, "VHPOSR did not change; bus may be floating/stalled.\n");
    rc = 1;
  }

  {
    uint16_t regs[3] = {0};
    const uint32_t addrs[3] = {0x00DFF002u, 0x00DFF01Eu, 0x00BFE001u};

    for (size_t i = 0; i < 3; i++) {
      regs[i] = ps_read_16(addrs[i]);
    }

    if ((regs[0] == 0x00FFu && regs[1] == 0x00FFu && regs[2] == 0x00FFu) ||
        (regs[0] == 0x0000u && regs[1] == 0x0000u && regs[2] == 0x0000u)) {
      fprintf(stderr, "Sampled registers look floating or zeroed.\n");
      rc = 1;
    }

    printf("DMACONR=0x%04x INTREQR=0x%04x CIAA_PRA=0x%04x\n", regs[0], regs[1], regs[2]);
  }

  ps_cleanup_protocol();
  return rc;
}
