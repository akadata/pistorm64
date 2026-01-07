// SPDX-License-Identifier: MIT

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "gpio/ps_protocol.h"

#define SIZE_KILO 1024u
#define SIZE_MEGA (1024u * 1024u)

struct region {
  const char *name;
  uint32_t base;
  uint32_t size;
};

static int check_emulator(void) {
  DIR *dir = opendir("/proc");
  if (!dir) {
    perror("can't open /proc, assuming emulator running");
    return 1;
  }

  struct dirent *ent;
  while ((ent = readdir(dir)) != NULL) {
    long pid = atol(ent->d_name);
    if (pid <= 0) {
      continue;
    }
    char buf[256];
    snprintf(buf, sizeof(buf), "/proc/%ld/stat", pid);
    FILE *fp = fopen(buf, "r");
    if (!fp) {
      continue;
    }
    char pname[128];
    char state;
    long rpid;
    if (fscanf(fp, "%ld (%127[^)]) %c", &rpid, pname, &state) == 3) {
      if (strcmp(pname, "emulator") == 0) {
        fclose(fp);
        closedir(dir);
        return 1;
      }
    }
    fclose(fp);
  }

  closedir(dir);
  return 0;
}

static int wait_txn_idle(const char *tag, int timeout_us) {
  while (timeout_us > 0) {
    if (!(*(gpio + 13) & (1 << PIN_TXN_IN_PROGRESS))) {
      return 0;
    }
    usleep(10);
    timeout_us -= 10;
  }
  printf("[RST] Warning: TXN_IN_PROGRESS still set after reset (%s)\n", tag);
  return -1;
}

static void warmup_bus(void) {
  for (int i = 0; i < 64; i++) {
    (void)ps_read_status_reg();
    if ((i & 0x0f) == 0) {
      usleep(100);
    }
  }
}

static void reset_amiga(const char *tag) {
  for (int attempt = 0; attempt < 3; attempt++) {
    ps_reset_state_machine();
    ps_pulse_reset();
    usleep(1500);
    if (wait_txn_idle(tag, 20000) == 0) {
      warmup_bus();
      return;
    }
    usleep(2000);
  }
}

static double elapsed_sec(const struct timespec *a, const struct timespec *b) {
  return (double)(b->tv_sec - a->tv_sec) +
         (double)(b->tv_nsec - a->tv_nsec) / 1000000000.0;
}

static double bench_write32(uint32_t base, uint32_t size) {
  uint32_t words = size / 4u;
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  for (uint32_t i = 0; i < words; i++) {
    uint32_t addr = base + (i * 4u);
    write32(addr, addr ^ 0xA5A5A5A5u);
  }
  clock_gettime(CLOCK_MONOTONIC, &t1);
  return elapsed_sec(&t0, &t1);
}

static double bench_read32(uint32_t base, uint32_t size, uint32_t *sink) {
  uint32_t words = size / 4u;
  struct timespec t0, t1;
  uint32_t acc = 0;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  for (uint32_t i = 0; i < words; i++) {
    uint32_t addr = base + (i * 4u);
    acc ^= read32(addr);
  }
  clock_gettime(CLOCK_MONOTONIC, &t1);
  *sink = acc;
  return elapsed_sec(&t0, &t1);
}

static void run_region(const struct region *r, int repeats) {
  if (r->size < 4u) {
    printf("[SKIP] %s size too small\n", r->name);
    return;
  }

  uint32_t size = r->size & ~3u;
  double best_w = 1e9, best_r = 1e9;
  uint32_t sink = 0;

  for (int i = 0; i < repeats; i++) {
    double tw = bench_write32(r->base, size);
    double tr = bench_read32(r->base, size, &sink);
    if (tw < best_w) best_w = tw;
    if (tr < best_r) best_r = tr;
  }

  double mb = (double)size / (1024.0 * 1024.0);
  double w_mbs = (best_w > 0.0) ? (mb / best_w) : 0.0;
  double r_mbs = (best_r > 0.0) ? (mb / best_r) : 0.0;

  printf("[REG] %-8s base=0x%06X size=%u KB | write32=%.2f MB/s read32=%.2f MB/s (sink=0x%08X)\n",
         r->name, r->base, size / SIZE_KILO, w_mbs, r_mbs, sink);
}

static int parse_region_arg(const char *arg, struct region *out) {
  // format: name:hex_base:size_kb (e.g., fast:0x200000:4096)
  char *tmp = strdup(arg);
  if (!tmp) return -1;
  char *name = strtok(tmp, ":");
  char *base = strtok(NULL, ":");
  char *size = strtok(NULL, ":");
  if (!name || !base || !size) {
    free(tmp);
    return -1;
  }
  out->name = strdup(name);
  out->base = (uint32_t)strtoul(base, NULL, 0);
  out->size = (uint32_t)strtoul(size, NULL, 0) * SIZE_KILO;
  free(tmp);
  return 0;
}

static void usage(const char *prog) {
  printf("Usage: %s [--chip-kb N] [--region name:base:size_kb] [--repeat N]\n", prog);
  printf("Default: chip ram 1024 KB at base 0x000000\n");
  printf("Example: %s --chip-kb 1024 --region fast:0x200000:8192 --repeat 3\n", prog);
}

int main(int argc, char *argv[]) {
  if (check_emulator()) {
    printf("PiStorm emulator running, please stop this before running benchmark\n");
    return 1;
  }

  struct region regions[8];
  int region_count = 0;
  int repeats = 1;
  uint32_t chip_kb = 1024;

  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--chip-kb") && i + 1 < argc) {
      chip_kb = (uint32_t)strtoul(argv[++i], NULL, 0);
    } else if (!strcmp(argv[i], "--region") && i + 1 < argc) {
      if (region_count < (int)(sizeof(regions) / sizeof(regions[0]))) {
        if (parse_region_arg(argv[++i], &regions[region_count]) == 0) {
          region_count++;
        } else {
          printf("Invalid --region format\n");
          return 1;
        }
      }
    } else if (!strcmp(argv[i], "--repeat") && i + 1 < argc) {
      repeats = atoi(argv[++i]);
      if (repeats < 1) repeats = 1;
    } else {
      usage(argv[0]);
      return 1;
    }
  }

  ps_setup_protocol();
  reset_amiga("startup");
  write8(0xbfe201, 0x0101); // CIA OVL
  write8(0xbfe001, 0x0000); // CIA OVL LOW

  if (region_count == 0) {
    regions[0].name = "chip";
    regions[0].base = 0x000000u;
    regions[0].size = chip_kb * SIZE_KILO;
    region_count = 1;
  }

  for (int i = 0; i < region_count; i++) {
    run_region(&regions[i], repeats);
  }

  return 0;
}

void m68k_set_irq(unsigned int level __attribute__((unused))) {
}
