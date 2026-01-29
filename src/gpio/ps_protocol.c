// SPDX-License-Identifier: MIT

/*
  Original Copyright 2020 Claude Schwarz
  Code reorganized and rewritten by
  Niklas Ekström 2021 (https://github.com/niklasekstrom)
*/

#define _XOPEN_SOURCE 600  // for usleep prototype with -std=c11
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stddef.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "ps_protocol.h"
#include "src/musashi/m68k.h"

volatile unsigned int *gpio;
volatile unsigned int *gpclk;

unsigned int gpfsel0;
unsigned int gpfsel1;
unsigned int gpfsel2;

unsigned int gpfsel0_o;
unsigned int gpfsel1_o;
unsigned int gpfsel2_o;

#define PS_WRITE_QUEUE_SIZE 2048u
#define PS_WRITE_QUEUE_MASK (PS_WRITE_QUEUE_SIZE - 1u)

typedef struct {
  uint32_t addr;
  uint32_t value;
  uint8_t size;
} ps_write_queue_item_t;

static ps_write_queue_item_t ps_write_queue[PS_WRITE_QUEUE_SIZE];
static uint32_t ps_write_queue_head = 0;
static uint32_t ps_write_queue_tail = 0;
static pthread_mutex_t ps_write_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ps_write_queue_not_empty = PTHREAD_COND_INITIALIZER;
static pthread_cond_t ps_write_queue_not_full = PTHREAD_COND_INITIALIZER;
static pthread_cond_t ps_write_queue_empty = PTHREAD_COND_INITIALIZER;
static pthread_t ps_write_queue_thread;
static bool ps_write_queue_thread_started = false;
static bool ps_write_queue_thread_stop = false;

typedef struct {
  uint64_t enqueued;
  uint64_t drained;
  uint32_t max_depth;
  uint64_t flush_calls;
} ps_write_stats_t;

static ps_write_stats_t ps_stats;

static void ps_write_8_direct(uint32_t address, uint8_t data);
static void ps_write_16_direct(uint32_t address, uint16_t data);
static void ps_write_queue_enqueue(uint32_t address, uint32_t value, uint8_t size);
static void ps_write_queue_flush(void);
static void* ps_write_queue_worker(void* arg);
static void ps_write_queue_start(void);

static void setup_io() {
  int fd = open("/dev/mem", O_RDWR | O_SYNC);
  if (fd < 0) {
    printf("Unable to open /dev/mem. Run as root using sudo?\n");
    exit(-1);
  }

  void *gpio_map = mmap(
      NULL,                    // Any adddress in our space will do
      BCM2708_PERI_SIZE,       // Map length
      PROT_READ | PROT_WRITE,  // Enable reading & writting to mapped memory
      MAP_SHARED,              // Shared with other processes
      fd,                      // File to map
      BCM2708_PERI_BASE        // Offset to GPIO peripheral
  );

  close(fd);

  if (gpio_map == MAP_FAILED) {
    printf("mmap failed, errno = %d\n", errno);
    exit(-1);
  }

  gpio = ((volatile unsigned *)gpio_map) + GPIO_ADDR / 4;
  gpclk = ((volatile unsigned *)gpio_map) + GPCLK_ADDR / 4;
}

static void setup_gpclk() {
  // Enable 200MHz CLK output on GPIO4, adjust divider and pll source depending
  // on pi model
  *(gpclk + (CLK_GP0_CTL / 4)) = CLK_PASSWD | (1 << 5);
  usleep(10);
  while ((*(gpclk + (CLK_GP0_CTL / 4))) & (1 << 7))
    ;
  usleep(100);
  *(gpclk + (CLK_GP0_DIV / 4)) =
      CLK_PASSWD | (6 << 12);  // divider , 6=200MHz on pi3
  usleep(10);
  *(gpclk + (CLK_GP0_CTL / 4)) =
      CLK_PASSWD | 5 | (1 << 4);  // pll? 6=plld, 5=pllc
  usleep(10);
  while (((*(gpclk + (CLK_GP0_CTL / 4))) & (1 << 7)) == 0)
    ;
  usleep(100);

  SET_GPIO_ALT(PIN_CLK, 0);  // gpclk0
}

void ps_setup_protocol() {
  setup_io();
  setup_gpclk();

  *(gpio + 10) = 0xffffec;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;

  ps_write_queue_start();
}

static inline bool ps_write_queue_enabled(void) {
  return ps_write_queue_thread_started;
}

static inline bool ps_write_queue_empty(void) {
  return (ps_write_queue_head == ps_write_queue_tail);
}

static void ps_flush_write_queue_blocking(void) {
  if (!ps_write_queue_thread_started) {
    return;
  }
  ps_write_queue_flush();
  ps_stats.flush_calls++;
}

static inline void ps_maybe_flush_write_queue(void) {
  if (ps_write_queue_thread_started && !ps_write_queue_empty()) {
    ps_flush_write_queue_blocking();
  }
}

static void ps_write_queue_enqueue(uint32_t address, uint32_t value, uint8_t size) {
  pthread_mutex_lock(&ps_write_queue_mutex);
  while ((ps_write_queue_head - ps_write_queue_tail) >= PS_WRITE_QUEUE_SIZE) {
    pthread_cond_wait(&ps_write_queue_not_full, &ps_write_queue_mutex);
  }
  uint32_t idx = ps_write_queue_head & PS_WRITE_QUEUE_MASK;
  ps_write_queue[idx].addr = address;
  ps_write_queue[idx].value = value;
  ps_write_queue[idx].size = size;
  ps_write_queue_head++;
  ps_stats.enqueued++;
  uint32_t depth =
      (ps_write_queue_head - ps_write_queue_tail) & PS_WRITE_QUEUE_MASK;
  if (depth > ps_stats.max_depth) {
    ps_stats.max_depth = depth;
  }
  pthread_cond_signal(&ps_write_queue_not_empty);
  pthread_mutex_unlock(&ps_write_queue_mutex);
}

static void ps_write_queue_flush(void) {
  pthread_mutex_lock(&ps_write_queue_mutex);
  while (ps_write_queue_tail != ps_write_queue_head) {
    pthread_cond_wait(&ps_write_queue_empty, &ps_write_queue_mutex);
  }
  pthread_mutex_unlock(&ps_write_queue_mutex);
}

static void* ps_write_queue_worker(void* unused) {
  (void)unused;
  while (1) {
    pthread_mutex_lock(&ps_write_queue_mutex);
    while (ps_write_queue_tail == ps_write_queue_head && !ps_write_queue_thread_stop) {
      pthread_cond_wait(&ps_write_queue_not_empty, &ps_write_queue_mutex);
    }
    if (ps_write_queue_tail == ps_write_queue_head && ps_write_queue_thread_stop) {
      pthread_mutex_unlock(&ps_write_queue_mutex);
      break;
    }
    uint32_t idx = ps_write_queue_tail & PS_WRITE_QUEUE_MASK;
    ps_write_queue_item_t item = ps_write_queue[idx];
    ps_write_queue_tail++;
    pthread_cond_signal(&ps_write_queue_not_full);
    if (ps_write_queue_tail == ps_write_queue_head) {
      pthread_cond_broadcast(&ps_write_queue_empty);
    }
    pthread_mutex_unlock(&ps_write_queue_mutex);

    ps_stats.drained++;
    switch (item.size) {
      case 1:
        ps_write_8_direct(item.addr, (uint8_t)item.value);
        break;
      case 2:
        ps_write_16_direct(item.addr, (uint16_t)item.value);
        break;
      case 4:
        ps_write_32_direct(item.addr, item.value);
        break;
      default:
        break;
    }
  }

  return NULL;
}

static void ps_write_queue_start(void) {
  if (ps_write_queue_thread_started) {
    return;
  }
  ps_write_queue_head = 0;
  ps_write_queue_tail = 0;
  ps_write_queue_thread_stop = false;
  if (pthread_create(&ps_write_queue_thread, NULL, ps_write_queue_worker, NULL) == 0) {
    pthread_detach(ps_write_queue_thread);
    ps_write_queue_thread_started = true;
  }
}

static inline bool ps_addr_is_slow_region(uint32_t addr) {
  // most Amiga hardware/register ranges live in the low 16/24-bit window
  return (addr & 0xFF000000u) == 0;
}

static void ps_write_16_direct(uint32_t address, uint16_t data) {
  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;

  *(gpio + 7) = ((data & 0xffff) << 8) | (REG_DATA << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 7) = ((address & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 7) = ((0x0200 | (address >> 16)) << 8) | (REG_ADDR_HI << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;

  while (*(gpio + 13) & (1 << PIN_TXN_IN_PROGRESS))
    ;
}

static void ps_write_8_direct(uint32_t address, uint8_t data) {
  unsigned int data_temp = data;
  if ((address & 1) == 0)
    data_temp = data_temp + (data_temp << 8);  // EVEN, A0=0,UDS
  else
    data_temp = data_temp & 0xff;  // ODD , A0=1,LDS

  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;

  *(gpio + 7) = ((data_temp & 0xffff) << 8) | (REG_DATA << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 7) = ((address & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 7) = ((0x0100 | (address >> 16)) << 8) | (REG_ADDR_HI << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;

  while (*(gpio + 13) & (1 << PIN_TXN_IN_PROGRESS))
    ;
}

static void ps_write_32_direct(uint32_t address, uint32_t value) {
  ps_write_16_direct(address, (uint16_t)(value >> 16));
  ps_write_16_direct(address + 2, (uint16_t)value);
}

void ps_write_16(uint32_t address, uint16_t data) {
  if (!ps_write_queue_enabled() || !ps_addr_is_slow_region(address)) {
    ps_write_16_direct(address, data);
    return;
  }
  ps_write_queue_enqueue(address, data, 2);
}

void ps_write_8(uint32_t address, uint8_t data) {
  if (!ps_write_queue_enabled() || !ps_addr_is_slow_region(address)) {
    ps_write_8_direct(address, data);
    return;
  }
  ps_write_queue_enqueue(address, data, 1);
}

void ps_write_32(uint32_t address, uint32_t value) {
  if (!ps_write_queue_enabled() || !ps_addr_is_slow_region(address)) {
    ps_write_32_direct(address, value);
    return;
  }
  ps_write_queue_enqueue(address, value, 4);
}

uint16_t ps_read_16(uint32_t address) {
  ps_maybe_flush_write_queue();
  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;

  *(gpio + 7) = ((address & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 7) = ((0x0200 | (address >> 16)) << 8) | (REG_ADDR_HI << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;

  *(gpio + 7) = (REG_DATA << PIN_A0);
  *(gpio + 7) = 1 << PIN_RD;

  while (*(gpio + 13) & (1 << PIN_TXN_IN_PROGRESS))
    ;

  unsigned int value = *(gpio + 13);

  *(gpio + 10) = 0xffffec;

  return (uint16_t)((value >> 8) & 0xffff);
}

uint8_t ps_read_8(uint32_t address) {
  ps_maybe_flush_write_queue();
  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;

  *(gpio + 7) = ((address & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 7) = ((0x0300 | (address >> 16)) << 8) | (REG_ADDR_HI << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;

  *(gpio + 7) = (REG_DATA << PIN_A0);
  *(gpio + 7) = 1 << PIN_RD;

  while (*(gpio + 13) & (1 << PIN_TXN_IN_PROGRESS))
    ;

  unsigned int value = *(gpio + 13);

  *(gpio + 10) = 0xffffec;

  value = (value >> 8) & 0xffff;

  if ((address & 1) == 0)
    return (uint8_t)((value >> 8) & 0xff);  // EVEN, A0=0,UDS
  else
    return (uint8_t)(value & 0xff);  // ODD , A0=1,LDS
}

uint32_t ps_read_32(uint32_t address) {
  ps_maybe_flush_write_queue();
  uint16_t a = ps_read_16(address);
  uint16_t b = ps_read_16(address + 2);
  return ((uint32_t)a << 16) | b;
}

void ps_write_status_reg(uint16_t value) {
  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;

  *(gpio + 7) = ((value & 0xffff) << 8) | (REG_STATUS << PIN_A0);

  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 7) = 1 << PIN_WR;  // delay
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;
}

uint16_t ps_read_status_reg() {
  ps_flush_write_queue_blocking();
  *(gpio + 7) = (REG_STATUS << PIN_A0);
  *(gpio + 7) = 1 << PIN_RD;
  *(gpio + 7) = 1 << PIN_RD;
  *(gpio + 7) = 1 << PIN_RD;
  *(gpio + 7) = 1 << PIN_RD;

  unsigned int value = *(gpio + 13);

  *(gpio + 10) = 0xffffec;

  return (uint16_t)((value >> 8) & 0xffff);
}

void ps_reset_state_machine() {
  ps_write_status_reg(STATUS_BIT_INIT);
  usleep(1500);
  ps_write_status_reg(0);
  usleep(100);
}

void ps_pulse_reset() {
  ps_write_status_reg(0);
  usleep(100000);
  ps_write_status_reg(STATUS_BIT_RESET);
}

unsigned int ps_get_ipl_zero() {
  ps_flush_write_queue_blocking();
  unsigned int value = *(gpio + 13);
  return value & (1 << PIN_IPL_ZERO);
}

unsigned int ps_gpio_lev() {
  ps_flush_write_queue_blocking();
  return *(gpio + 13);
}

#define INT2_ENABLED 1

void ps_update_irq() {
  unsigned int ipl = 0;

  if (!ps_get_ipl_zero()) {
    unsigned int status = ps_read_status_reg();
    ipl = (status & 0xe000) >> 13;
  }

  /*if (ipl < 2 && INT2_ENABLED && emu_int2_req()) {
    ipl = 2;
  }*/

  m68k_set_irq(ipl);
}

void ps_protocol_dump_stats(void) {
  printf("[PS_PROTO] writes: enq=%" PRIu64 " drained=%" PRIu64 " max_depth=%u flush_calls=%" PRIu64 "\n",
         ps_stats.enqueued, ps_stats.drained, ps_stats.max_depth, ps_stats.flush_calls);
}
