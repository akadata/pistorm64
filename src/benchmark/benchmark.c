// SPDX-License-Identifier: MIT

#include <dirent.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "gpio/ps_protocol.h"

#define SIZE_KILO 1024u
#define SIZE_MEGA (1024u * 1024u)

extern volatile unsigned int *gpio;

struct wait_stats {
  uint64_t count;
  uint64_t total_us;
  uint32_t max_us;
  uint32_t *samples;
  uint32_t sample_count;
  uint32_t sample_max;
  uint32_t sample_stride;
  uint32_t sample_next;
  uint32_t timing_stride;
  uint32_t timing_next;
  uint32_t last_p95;
};

static volatile const char *g_smoke_phase = NULL;
static uint32_t g_wait_timing_stride = 1;
static int g_pacing_kind = 0;

enum pacing_mode {
  PACING_TXN = 0,
  PACING_BURST = 1,
};

enum pacing_kind {
  PACING_SLEEP = 0,
  PACING_SPIN = 1,
};

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

static uint32_t wait_txn_timed_us(void) {
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  while (*(gpio + 13) & (1 << PIN_TXN_IN_PROGRESS)) {
  }
  clock_gettime(CLOCK_MONOTONIC, &t1);
  uint64_t us = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000ull;
  if (t1.tv_nsec >= t0.tv_nsec) {
    us += (uint64_t)(t1.tv_nsec - t0.tv_nsec) / 1000ull;
  } else {
    us -= 1000000ull;
    us += (uint64_t)(1000000000ull + t1.tv_nsec - t0.tv_nsec) / 1000ull;
  }
  return (uint32_t)us;
}

static void wait_txn_spin(void) {
  while (*(gpio + 13) & (1 << PIN_TXN_IN_PROGRESS)) {
  }
}

static void pacing_delay_us(int pacing_us) {
  if (pacing_us <= 0) return;
  if (g_pacing_kind == PACING_SLEEP) {
    usleep((useconds_t)pacing_us);
    return;
  }

  struct timespec t0, tn;
  uint64_t target_ns = (uint64_t)pacing_us * 1000ull;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  for (;;) {
    clock_gettime(CLOCK_MONOTONIC, &tn);
    uint64_t elapsed_ns = (uint64_t)(tn.tv_sec - t0.tv_sec) * 1000000000ull;
    if (tn.tv_nsec >= t0.tv_nsec) {
      elapsed_ns += (uint64_t)(tn.tv_nsec - t0.tv_nsec);
    } else {
      elapsed_ns -= 1000000000ull;
      elapsed_ns += (uint64_t)(1000000000ull + tn.tv_nsec - t0.tv_nsec);
    }
    if (elapsed_ns >= target_ns) break;
  }
}
static void stats_init(struct wait_stats *st, uint64_t total_txns, uint32_t *samples, uint32_t max_samples) {
  st->count = 0;
  st->total_us = 0;
  st->max_us = 0;
  st->samples = samples;
  st->sample_count = 0;
  st->sample_max = max_samples;
  if (max_samples > 0 && total_txns > max_samples) {
    st->sample_stride = (uint32_t)((total_txns + max_samples - 1) / max_samples);
    if (st->sample_stride == 0) st->sample_stride = 1;
  } else {
    st->sample_stride = 1;
  }
  st->sample_next = 0;
  st->timing_stride = g_wait_timing_stride ? g_wait_timing_stride : 1;
  st->timing_next = 0;
  st->last_p95 = 0;
}

static void stats_update(struct wait_stats *st, uint32_t wait_us) {
  st->count++;
  st->total_us += wait_us;
  if (wait_us > st->max_us) st->max_us = wait_us;
  if (st->sample_stride > 0) {
    if (st->sample_next == 0) {
      if (st->samples && st->sample_count < st->sample_max) {
        st->samples[st->sample_count++] = wait_us;
      }
      st->sample_next = st->sample_stride;
    }
    st->sample_next--;
  }
}

static void stats_wait(struct wait_stats *st) {
  if (st->timing_stride <= 1) {
    uint32_t wait_us = wait_txn_timed_us();
    stats_update(st, wait_us);
    return;
  }

  if (st->timing_next == 0) {
    uint32_t wait_us = wait_txn_timed_us();
    stats_update(st, wait_us);
    st->timing_next = st->timing_stride - 1;
    return;
  }

  wait_txn_spin();
  st->timing_next--;
}

static int cmp_u32(const void *a, const void *b) {
  uint32_t va = *(const uint32_t *)a;
  uint32_t vb = *(const uint32_t *)b;
  if (va < vb) return -1;
  if (va > vb) return 1;
  return 0;
}

static void stats_report(const struct wait_stats *st, char *out, size_t outlen) {
  if (st->count == 0 || st->sample_count == 0) {
    snprintf(out, outlen, "avg=? p95=? max=?");
    return;
  }
  uint32_t *sorted = malloc(sizeof(uint32_t) * st->sample_count);
  if (!sorted) {
    snprintf(out, outlen, "avg=? p95=? max=%u", st->max_us);
    return;
  }
  memcpy(sorted, st->samples, sizeof(uint32_t) * st->sample_count);
  qsort(sorted, st->sample_count, sizeof(uint32_t), cmp_u32);
  uint32_t idx = (uint32_t)((st->sample_count * 95ull + 99ull) / 100ull);
  if (idx == 0) idx = 1;
  if (idx > st->sample_count) idx = st->sample_count;
  uint32_t p95 = sorted[idx - 1];
  free(sorted);
  ((struct wait_stats *)st)->last_p95 = p95;
  double avg = (double)st->total_us / (double)st->count;
  snprintf(out, outlen, "avg=%.2f p95=%u max=%u", avg, p95, st->max_us);
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

static uint32_t gpfsel0_data_in;
static uint32_t gpfsel1_data_in;
static uint32_t gpfsel2_data_in;
static int gpfsel_data_in_ready = 0;

static void init_gpfsel_data_in(void) {
  if (gpfsel_data_in_ready) return;

  gpfsel0_data_in = GPFSEL0_OUTPUT;
  gpfsel1_data_in = GPFSEL1_OUTPUT;
  gpfsel2_data_in = GPFSEL2_OUTPUT;

  // Clear FSEL bits for data pins 8..23 to make them inputs.
  for (int pin = 8; pin <= 23; pin++) {
    uint32_t mask = 0x7u << ((pin % 10) * 3);
    if (pin <= 9) {
      gpfsel0_data_in &= ~mask;
    } else if (pin <= 19) {
      gpfsel1_data_in &= ~mask;
    } else {
      gpfsel2_data_in &= ~mask;
    }
  }

  gpfsel_data_in_ready = 1;
}

static inline void set_gpfsel_data_in(void) {
  init_gpfsel_data_in();
  *(gpio + 0) = gpfsel0_data_in;
  *(gpio + 1) = gpfsel1_data_in;
  *(gpio + 2) = gpfsel2_data_in;
}

static inline void write8_raw(uint32_t address, uint8_t data, struct wait_stats *st) {
  uint32_t v = (address & 0x01) ? (data & 0xFFu) : ((uint32_t)data | ((uint32_t)data << 8));
  GPIO_WRITEREG(REG_DATA, (v & 0xFFFFu));
  GPIO_WRITEREG(REG_ADDR_LO, (address & 0xFFFFu));
  GPIO_WRITEREG(REG_ADDR_HI, (0x0100u | (address >> 16)));
  stats_wait(st);
}

static inline uint8_t read8_raw(uint32_t address, struct wait_stats *st) {
  GPIO_WRITEREG(REG_ADDR_LO, (address & 0xFFFFu));
  GPIO_WRITEREG(REG_ADDR_HI, (0x0300u | (address >> 16)));
  GPIO_PIN_RD;
  stats_wait(st);
  uint32_t value = ((*(gpio + 13) >> 8) & 0xFFFFu);
  END_TXN;
  if (address & 0x01) return value & 0xFFu;
  return (value >> 8) & 0xFFu;
}

static inline void write16_raw(uint32_t address, uint16_t data, struct wait_stats *st) {
  GPIO_WRITEREG(REG_DATA, (data & 0xFFFFu));
  GPIO_WRITEREG(REG_ADDR_LO, (address & 0xFFFFu));
  GPIO_WRITEREG(REG_ADDR_HI, (0x0000u | (address >> 16)));
  stats_wait(st);
}

static inline uint16_t read16_raw(uint32_t address, struct wait_stats *st) {
  GPIO_WRITEREG(REG_ADDR_LO, (address & 0xFFFFu));
  GPIO_WRITEREG(REG_ADDR_HI, (0x0200u | (address >> 16)));
  GPIO_PIN_RD;
  stats_wait(st);
  uint16_t value = (uint16_t)(((*(gpio + 13) >> 8) & 0xFFFFu));
  END_TXN;
  return value;
}

static inline void write32_raw(uint32_t address, uint32_t data, struct wait_stats *st) {
  write16_raw(address, (uint16_t)(data >> 16), st);
  write16_raw(address + 2u, (uint16_t)data, st);
}

static inline uint32_t read32_raw(uint32_t address, struct wait_stats *st) {
  uint32_t hi = read16_raw(address, st);
  uint32_t lo = read16_raw(address + 2u, st);
  return (hi << 16) | lo;
}

static double bench_write8(uint32_t base, uint32_t size, int burst, int pacing_us, int pacing_mode, struct wait_stats *st) {
  uint32_t bytes = size;
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  for (uint32_t i = 0; i < bytes;) {
    uint32_t todo = (uint32_t)burst;
    if (todo > bytes - i) todo = bytes - i;
    GPFSEL_OUTPUT;
    for (uint32_t j = 0; j < todo; j++) {
      uint32_t addr = base + i + j;
      write8_raw(addr, (uint8_t)(addr ^ 0xA5u), st);
      if (pacing_mode == PACING_TXN) pacing_delay_us(pacing_us);
    }
    GPFSEL_INPUT;
    if (pacing_mode == PACING_BURST) pacing_delay_us(pacing_us);
    i += todo;
  }
  clock_gettime(CLOCK_MONOTONIC, &t1);
  return elapsed_sec(&t0, &t1);
}

static double bench_read8(uint32_t base, uint32_t size, int burst, int pacing_us, int pacing_mode, struct wait_stats *st, uint32_t *sink) {
  uint32_t bytes = size;
  struct timespec t0, t1;
  uint32_t acc = 0;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  for (uint32_t i = 0; i < bytes;) {
    uint32_t todo = (uint32_t)burst;
    if (todo > bytes - i) todo = bytes - i;
    GPFSEL_OUTPUT;
    set_gpfsel_data_in();
    for (uint32_t j = 0; j < todo; j++) {
      uint32_t addr = base + i + j;
      uint8_t v = read8_raw(addr, st);
      acc ^= v;
      if (pacing_mode == PACING_TXN) pacing_delay_us(pacing_us);
    }
    GPFSEL_INPUT;
    if (pacing_mode == PACING_BURST) pacing_delay_us(pacing_us);
    i += todo;
  }
  clock_gettime(CLOCK_MONOTONIC, &t1);
  *sink = acc;
  return elapsed_sec(&t0, &t1);
}

static double bench_write16(uint32_t base, uint32_t size, int burst, int pacing_us, int pacing_mode, struct wait_stats *st) {
  uint32_t words = size / 2u;
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  for (uint32_t i = 0; i < words;) {
    uint32_t todo = (uint32_t)burst;
    if (todo > words - i) todo = words - i;
    GPFSEL_OUTPUT;
    for (uint32_t j = 0; j < todo; j++) {
      uint32_t addr = base + ((i + j) * 2u);
      write16_raw(addr, (uint16_t)(addr ^ 0xA5A5u), st);
      if (pacing_mode == PACING_TXN) pacing_delay_us(pacing_us);
    }
    GPFSEL_INPUT;
    if (pacing_mode == PACING_BURST) pacing_delay_us(pacing_us);
    i += todo;
  }
  clock_gettime(CLOCK_MONOTONIC, &t1);
  return elapsed_sec(&t0, &t1);
}

static double bench_read16(uint32_t base, uint32_t size, int burst, int pacing_us, int pacing_mode, struct wait_stats *st, uint32_t *sink) {
  uint32_t words = size / 2u;
  struct timespec t0, t1;
  uint32_t acc = 0;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  for (uint32_t i = 0; i < words;) {
    uint32_t todo = (uint32_t)burst;
    if (todo > words - i) todo = words - i;
    GPFSEL_OUTPUT;
    set_gpfsel_data_in();
    for (uint32_t j = 0; j < todo; j++) {
      uint32_t addr = base + ((i + j) * 2u);
      uint16_t v = read16_raw(addr, st);
      acc ^= v;
      if (pacing_mode == PACING_TXN) pacing_delay_us(pacing_us);
    }
    GPFSEL_INPUT;
    if (pacing_mode == PACING_BURST) pacing_delay_us(pacing_us);
    i += todo;
  }
  clock_gettime(CLOCK_MONOTONIC, &t1);
  *sink = acc;
  return elapsed_sec(&t0, &t1);
}

static double bench_write32(uint32_t base, uint32_t size, int burst, int pacing_us, int pacing_mode, struct wait_stats *st) {
  uint32_t words = size / 4u;
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  for (uint32_t i = 0; i < words;) {
    uint32_t todo = (uint32_t)burst;
    if (todo > words - i) todo = words - i;
    GPFSEL_OUTPUT;
    for (uint32_t j = 0; j < todo; j++) {
      uint32_t addr = base + ((i + j) * 4u);
      write32_raw(addr, addr ^ 0xA5A5A5A5u, st);
      if (pacing_mode == PACING_TXN) pacing_delay_us(pacing_us);
    }
    GPFSEL_INPUT;
    if (pacing_mode == PACING_BURST) pacing_delay_us(pacing_us);
    i += todo;
  }
  clock_gettime(CLOCK_MONOTONIC, &t1);
  return elapsed_sec(&t0, &t1);
}

static double bench_read32(uint32_t base, uint32_t size, int burst, int pacing_us, int pacing_mode, struct wait_stats *st, uint32_t *sink) {
  uint32_t words = size / 4u;
  struct timespec t0, t1;
  uint32_t acc = 0;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  for (uint32_t i = 0; i < words;) {
    uint32_t todo = (uint32_t)burst;
    if (todo > words - i) todo = words - i;
    GPFSEL_OUTPUT;
    set_gpfsel_data_in();
    for (uint32_t j = 0; j < todo; j++) {
      uint32_t addr = base + ((i + j) * 4u);
      uint32_t v = read32_raw(addr, st);
      acc ^= v;
      if (pacing_mode == PACING_TXN && pacing_us > 0) usleep((useconds_t)pacing_us);
    }
    GPFSEL_INPUT;
    if (pacing_mode == PACING_BURST && pacing_us > 0) usleep((useconds_t)pacing_us);
    i += todo;
  }
  clock_gettime(CLOCK_MONOTONIC, &t1);
  *sink = acc;
  return elapsed_sec(&t0, &t1);
}

static void run_region(const struct region *r, int repeats, int burst, int pacing_us, int pacing_mode) {
  if (r->size < 4u) {
    printf("[SKIP] %s size too small\n", r->name);
    return;
  }

  uint32_t size = r->size & ~3u;
  double best_w8 = 1e9, best_r8 = 1e9;
  double best_w16 = 1e9, best_r16 = 1e9;
  double best_w32 = 1e9, best_r32 = 1e9;
  struct wait_stats st_w8, st_r8, st_w16, st_r16, st_w32, st_r32;
  uint32_t samples_w8[10000], samples_r8[10000];
  uint32_t samples_w16[10000], samples_r16[10000];
  uint32_t samples_w32[10000], samples_r32[10000];
  uint32_t sink = 0;

  for (int i = 0; i < repeats; i++) {
    stats_init(&st_w8, size, samples_w8, 10000);
    stats_init(&st_r8, size, samples_r8, 10000);
    stats_init(&st_w16, size / 2u, samples_w16, 10000);
    stats_init(&st_r16, size / 2u, samples_r16, 10000);
    stats_init(&st_w32, size / 4u, samples_w32, 10000);
    stats_init(&st_r32, size / 4u, samples_r32, 10000);
    double tw8 = bench_write8(r->base, size, burst, pacing_us, pacing_mode, &st_w8);
    double tr8 = bench_read8(r->base, size, burst, pacing_us, pacing_mode, &st_r8, &sink);
    double tw16 = bench_write16(r->base, size, burst, pacing_us, pacing_mode, &st_w16);
    double tr16 = bench_read16(r->base, size, burst, pacing_us, pacing_mode, &st_r16, &sink);
    double tw32 = bench_write32(r->base, size, burst, pacing_us, pacing_mode, &st_w32);
    double tr32 = bench_read32(r->base, size, burst, pacing_us, pacing_mode, &st_r32, &sink);
    if (tw8 < best_w8) best_w8 = tw8;
    if (tr8 < best_r8) best_r8 = tr8;
    if (tw16 < best_w16) best_w16 = tw16;
    if (tr16 < best_r16) best_r16 = tr16;
    if (tw32 < best_w32) best_w32 = tw32;
    if (tr32 < best_r32) best_r32 = tr32;
  }

  double mb = (double)size / (1024.0 * 1024.0);
  double w8_mbs = (best_w8 > 0.0) ? (mb / best_w8) : 0.0;
  double r8_mbs = (best_r8 > 0.0) ? (mb / best_r8) : 0.0;
  double w16_mbs = (best_w16 > 0.0) ? (mb / best_w16) : 0.0;
  double r16_mbs = (best_r16 > 0.0) ? (mb / best_r16) : 0.0;
  double w32_mbs = (best_w32 > 0.0) ? (mb / best_w32) : 0.0;
  double r32_mbs = (best_r32 > 0.0) ? (mb / best_r32) : 0.0;
  char w8_stats[96], r8_stats[96], w16_stats[96], r16_stats[96], w32_stats[96], r32_stats[96];
  stats_report(&st_w8, w8_stats, sizeof(w8_stats));
  stats_report(&st_r8, r8_stats, sizeof(r8_stats));
  stats_report(&st_w16, w16_stats, sizeof(w16_stats));
  stats_report(&st_r16, r16_stats, sizeof(r16_stats));
  stats_report(&st_w32, w32_stats, sizeof(w32_stats));
  stats_report(&st_r32, r32_stats, sizeof(r32_stats));

  printf("[REG] %-8s burst=%u base=0x%06X size=%u KB | w8=%.2f r8=%.2f w16=%.2f r16=%.2f w32=%.2f r32=%.2f MB/s (sink=0x%08X)\n",
         r->name, burst, r->base, size / SIZE_KILO, w8_mbs, r8_mbs, w16_mbs, r16_mbs, w32_mbs, r32_mbs, sink);
  printf("       wait_us: w8[%s] r8[%s] w16[%s] r16[%s] w32[%s] r32[%s]\n",
         w8_stats, r8_stats, w16_stats, r16_stats, w32_stats, r32_stats);
}

static void smoke_sig_handler(int sig) {
  const volatile char *phase = g_smoke_phase ? g_smoke_phase : "unknown";
  fprintf(stderr, "[SMOKE] Crash during %s (signal %d)\n", (const char *)phase, sig);
  _exit(1);
}

static int run_smoke(uint32_t base, uint32_t size, int burst) {
  struct wait_stats st;
  uint32_t samples[2048];
  uint32_t sink = 0;

  signal(SIGSEGV, smoke_sig_handler);
  signal(SIGBUS, smoke_sig_handler);

  g_smoke_phase = "r16";
  printf("[SMOKE] r16 burst=%d size=%u KB\n", burst, size / SIZE_KILO);
  stats_init(&st, size / 2u, samples, 2048);
  (void)bench_read16(base, size, burst, 0, PACING_TXN, &st, &sink);

  g_smoke_phase = "w16";
  printf("[SMOKE] w16 burst=%d size=%u KB\n", burst, size / SIZE_KILO);
  stats_init(&st, size / 2u, samples, 2048);
  (void)bench_write16(base, size, burst, 0, PACING_TXN, &st);

  g_smoke_phase = NULL;
  printf("[SMOKE] ok\n");
  return 0;
}

static int report_error(uint32_t addr, uint16_t expected, uint16_t got, uint32_t *printed, uint32_t max_print) {
  if (*printed < max_print) {
    printf("  ERR @0x%06X exp=0x%04X got=0x%04X\n", addr, expected, got);
    (*printed)++;
  }
  return 1;
}

static int memtest_region(const struct region *r) {
  uint32_t size = r->size & ~1u;
  uint32_t printed = 0;
  uint32_t total_errors = 0;

  printf("[MEMTEST] %s base=0x%06X size=%u KB\n", r->name, r->base, size / SIZE_KILO);

  // Address test
  printf("  Address test...\n");
  for (uint32_t off = 0; off < size; off += 2) {
    uint16_t v = (uint16_t)((r->base + off) >> 1);
    write16(r->base + off, v);
  }
  for (uint32_t off = 0; off < size; off += 2) {
    uint16_t exp = (uint16_t)((r->base + off) >> 1);
    uint16_t got = read16(r->base + off);
    if (got != exp) {
      total_errors += report_error(r->base + off, exp, got, &printed, 16);
    }
  }

  // Walking 1s/0s
  printf("  Walking bits...\n");
  for (int bit = 0; bit < 16; bit++) {
    uint16_t pat = (uint16_t)(1u << bit);
    for (uint32_t off = 0; off < size; off += 2) write16(r->base + off, pat);
    for (uint32_t off = 0; off < size; off += 2) {
      uint16_t got = read16(r->base + off);
      if (got != pat) total_errors += report_error(r->base + off, pat, got, &printed, 16);
    }
    pat = (uint16_t)~pat;
    for (uint32_t off = 0; off < size; off += 2) write16(r->base + off, pat);
    for (uint32_t off = 0; off < size; off += 2) {
      uint16_t got = read16(r->base + off);
      if (got != pat) total_errors += report_error(r->base + off, pat, got, &printed, 16);
    }
  }

  // Fixed patterns
  const uint16_t patterns[] = {0x0000, 0xFFFF, 0xAAAA, 0x5555, 0xA5A5, 0x5A5A};
  printf("  Fixed patterns...\n");
  for (size_t p = 0; p < sizeof(patterns) / sizeof(patterns[0]); p++) {
    uint16_t pat = patterns[p];
    for (uint32_t off = 0; off < size; off += 2) write16(r->base + off, pat);
    for (uint32_t off = 0; off < size; off += 2) {
      uint16_t got = read16(r->base + off);
      if (got != pat) total_errors += report_error(r->base + off, pat, got, &printed, 16);
    }
  }

  // Random pattern (two-pass with seed)
  unsigned int seed = (unsigned int)time(NULL);
  printf("  Random pattern (seed=%u)...\n", seed);
  srand(seed);
  for (uint32_t off = 0; off < size; off += 2) {
    uint16_t pat = (uint16_t)rand();
    write16(r->base + off, pat);
  }
  srand(seed);
  for (uint32_t off = 0; off < size; off += 2) {
    uint16_t pat = (uint16_t)rand();
    uint16_t got = read16(r->base + off);
    if (got != pat) total_errors += report_error(r->base + off, pat, got, &printed, 16);
  }

  if (total_errors == 0) {
    printf("  OK\n");
  } else {
    printf("  FAIL: %u errors (showing first %u)\n", total_errors, printed);
  }
  return (total_errors == 0) ? 0 : 1;
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
  printf("Usage: %s [--chip-kb N] [--region name:base:size_kb] [--repeat N] [--burst N] [--pacing-us N] [--pacing-mode txn|burst] [--pacing-kind sleep|spin] [--pacing-sweep min:max:step] [--sweep-burst N] [--wait-sample N] [--smoke] [--memtest]\n", prog);
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
  int burst_sizes[8] = {1, 4, 16, 64};
  int burst_count = 4;
  int pacing_us = 0;
  int smoke = 0;
  int memtest = 0;
  int sweep_enabled = 0;
  int sweep_min = 0, sweep_max = 0, sweep_step = 1;
  int sweep_burst = 16;
  int pacing_mode = PACING_TXN;

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
    } else if (!strcmp(argv[i], "--burst") && i + 1 < argc) {
      burst_count = 0;
      int b = atoi(argv[++i]);
      if (b < 1) b = 1;
      burst_sizes[burst_count++] = b;
    } else if (!strcmp(argv[i], "--pacing-us") && i + 1 < argc) {
      pacing_us = atoi(argv[++i]);
      if (pacing_us < 0) pacing_us = 0;
    } else if (!strcmp(argv[i], "--pacing-mode") && i + 1 < argc) {
      const char *mode = argv[++i];
      if (strcmp(mode, "txn") == 0) {
        pacing_mode = PACING_TXN;
      } else if (strcmp(mode, "burst") == 0) {
        pacing_mode = PACING_BURST;
      } else {
        printf("Invalid --pacing-mode, expected txn|burst\n");
        return 1;
      }
    } else if (!strcmp(argv[i], "--pacing-kind") && i + 1 < argc) {
      const char *kind = argv[++i];
      if (strcmp(kind, "sleep") == 0) {
        g_pacing_kind = PACING_SLEEP;
      } else if (strcmp(kind, "spin") == 0) {
        g_pacing_kind = PACING_SPIN;
      } else {
        printf("Invalid --pacing-kind, expected sleep|spin\n");
        return 1;
      }
    } else if (!strcmp(argv[i], "--pacing-sweep") && i + 1 < argc) {
      const char *spec = argv[++i];
      int a = 0, b = 0, c = 0;
      if (sscanf(spec, "%d:%d:%d", &a, &b, &c) != 3 || c <= 0 || a < 0 || b < a) {
        printf("Invalid --pacing-sweep format, expected min:max:step\n");
        return 1;
      }
      sweep_enabled = 1;
      sweep_min = a;
      sweep_max = b;
      sweep_step = c;
    } else if (!strcmp(argv[i], "--sweep-burst") && i + 1 < argc) {
      sweep_burst = atoi(argv[++i]);
      if (sweep_burst < 1) sweep_burst = 1;
    } else if (!strcmp(argv[i], "--wait-sample") && i + 1 < argc) {
      int s = atoi(argv[++i]);
      if (s < 1) s = 1;
      g_wait_timing_stride = (uint32_t)s;
    } else if (!strcmp(argv[i], "--smoke")) {
      smoke = 1;
    } else if (!strcmp(argv[i], "--memtest")) {
      memtest = 1;
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

  if (smoke) {
    uint32_t size = 64 * SIZE_KILO;
    int burst = 16;
    printf("[SMOKE] starting\n");
    return run_smoke(regions[0].base, size, burst);
  }

  if (memtest) {
    int any_fail = 0;
    for (int i = 0; i < region_count; i++) {
      any_fail |= memtest_region(&regions[i]);
    }
    return any_fail ? 1 : 0;
  }

  if (sweep_enabled) {
    const struct region *r = &regions[0];
    uint32_t size = r->size & ~3u;
    uint32_t sink = 0;
    struct wait_stats st_w16, st_r16, st_w32, st_r32;
    uint32_t samples_w16[10000], samples_r16[10000];
    uint32_t samples_w32[10000], samples_r32[10000];
    int burst = sweep_burst;

    uint32_t txns16 = size / 2u;
    uint32_t txns32 = size / 4u;
    printf("[SWEEPDBG] min=%d max=%d step=%d burst=%d size_kb=%u repeat=%d wait_sample=%u mode=%s kind=%s\n",
           sweep_min, sweep_max, sweep_step, burst, size / SIZE_KILO, repeats, g_wait_timing_stride,
           pacing_mode == PACING_BURST ? "burst" : "txn",
           g_pacing_kind == PACING_SPIN ? "spin" : "sleep");
    printf("[SWEEP] region=%s base=0x%06X size=%u KB burst=%d repeat=%d wait_sample=%u\n",
           r->name, r->base, size / SIZE_KILO, burst, repeats, g_wait_timing_stride);
    printf("[SWEEP] pacing_us w16 r16 w32 r32 r32_16eq wait_p95 wait_max txns16 txns32 exp_delay_us16 exp_delay_us32\n");

    for (int p = sweep_min; p <= sweep_max; p += sweep_step) {
      double best_w16 = 1e9, best_r16 = 1e9, best_w32 = 1e9, best_r32 = 1e9;
      uint32_t p95 = 0, max_us = 0;

      for (int i = 0; i < repeats; i++) {
        stats_init(&st_w16, size / 2u, samples_w16, 10000);
        stats_init(&st_r16, size / 2u, samples_r16, 10000);
        stats_init(&st_w32, size / 4u, samples_w32, 10000);
        stats_init(&st_r32, size / 4u, samples_r32, 10000);
        double tw16 = bench_write16(r->base, size, burst, p, pacing_mode, &st_w16);
        double tr16 = bench_read16(r->base, size, burst, p, pacing_mode, &st_r16, &sink);
        double tw32 = bench_write32(r->base, size, burst, p, pacing_mode, &st_w32);
        double tr32 = bench_read32(r->base, size, burst, p, pacing_mode, &st_r32, &sink);
        if (tw16 < best_w16) best_w16 = tw16;
        if (tr16 < best_r16) best_r16 = tr16;
        if (tw32 < best_w32) best_w32 = tw32;
        if (tr32 < best_r32) best_r32 = tr32;
      }

      char tmp[64];
      stats_report(&st_w16, tmp, sizeof(tmp));
      if (st_w16.last_p95 > p95) p95 = st_w16.last_p95;
      if (st_w16.max_us > max_us) max_us = st_w16.max_us;
      stats_report(&st_r16, tmp, sizeof(tmp));
      if (st_r16.last_p95 > p95) p95 = st_r16.last_p95;
      if (st_r16.max_us > max_us) max_us = st_r16.max_us;
      stats_report(&st_w32, tmp, sizeof(tmp));
      if (st_w32.last_p95 > p95) p95 = st_w32.last_p95;
      if (st_w32.max_us > max_us) max_us = st_w32.max_us;
      stats_report(&st_r32, tmp, sizeof(tmp));
      if (st_r32.last_p95 > p95) p95 = st_r32.last_p95;
      if (st_r32.max_us > max_us) max_us = st_r32.max_us;

      double mb = (double)size / (1024.0 * 1024.0);
      double w16_mbs = (best_w16 > 0.0) ? (mb / best_w16) : 0.0;
      double r16_mbs = (best_r16 > 0.0) ? (mb / best_r16) : 0.0;
      double w32_mbs = (best_w32 > 0.0) ? (mb / best_w32) : 0.0;
      double r32_mbs = (best_r32 > 0.0) ? (mb / best_r32) : 0.0;
      double r32_16eq_mbs = r32_mbs * 2.0;
      uint32_t bursts16 = (txns16 + (uint32_t)burst - 1u) / (uint32_t)burst;
      uint32_t bursts32 = (txns32 + (uint32_t)burst - 1u) / (uint32_t)burst;
      uint64_t exp_delay_us16 = 0;
      uint64_t exp_delay_us32 = 0;
      if (pacing_mode == PACING_BURST) {
        exp_delay_us16 = (uint64_t)bursts16 * (uint64_t)p;
        exp_delay_us32 = (uint64_t)bursts32 * (uint64_t)p;
      } else {
        exp_delay_us16 = (uint64_t)txns16 * (uint64_t)p;
        exp_delay_us32 = (uint64_t)txns32 * (uint64_t)p;
      }

      printf("%9d %.2f %.2f %.2f %.2f %.2f %8u %8u %7u %7u %13llu %13llu\n",
             p, w16_mbs, r16_mbs, w32_mbs, r32_mbs, r32_16eq_mbs,
             p95, max_us, txns16, txns32,
             (unsigned long long)exp_delay_us16, (unsigned long long)exp_delay_us32);
      fflush(stdout);
    }
    return 0;
  }

  for (int i = 0; i < region_count; i++) {
    for (int b = 0; b < burst_count; b++) {
      run_region(&regions[i], repeats, burst_sizes[b], pacing_us, pacing_mode);
    }
  }

  return 0;
}

void m68k_set_irq(unsigned int level __attribute__((unused))) {
}
