// SPDX-License-Identifier: MIT
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <pty.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "serial_echo.h"
#include "log.h"
#include "platforms/amiga/amiga-autoconf.h"
#include "platforms/amiga/pistorm-dev/pistorm-dev-enums.h"

#define Z2_SERIAL_SIZE 0x100
#define REG_DATA 0x00
#define REG_STATUS 0x04
#define REG_CTRL 0x08
#define STATUS_RX_READY 0x01
#define STATUS_TX_READY 0x02
#define RX_BUF_SIZE 256

typedef struct {
  uint8_t buf[RX_BUF_SIZE];
  uint16_t head;
  uint16_t tail;
  int pty_master;
  int pty_slave;
  char pty_name[64];
  pthread_t rx_thread;
  int rx_thread_running;
} z2_serial_state_t;

static z2_serial_state_t serial_state;

static void z2_serial_rx_enqueue(z2_serial_state_t *st, uint8_t value) {
  uint16_t next = (uint16_t)((st->head + 1u) % RX_BUF_SIZE);
  if (next != st->tail) {
    st->buf[st->head] = value;
    st->head = next;
  }
}

static void *z2_serial_rx_thread(void *arg) {
  z2_serial_state_t *st = (z2_serial_state_t *)arg;
  uint8_t buf[128];
  while (st->rx_thread_running) {
    ssize_t rd = read(st->pty_master, buf, sizeof(buf));
    if (rd > 0) {
      for (ssize_t i = 0; i < rd; i++) {
        z2_serial_rx_enqueue(st, buf[i]);
      }
      continue;
    }
    if (rd == 0) {
      usleep(1000);
      continue;
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      usleep(1000);
      continue;
    }
    usleep(1000);
  }
  return NULL;
}

static void z2_serial_host_init(z2_serial_state_t *st) {
  st->pty_master = -1;
  st->pty_slave = -1;
  st->pty_name[0] = '\0';
  if (openpty(&st->pty_master, &st->pty_slave, st->pty_name, NULL, NULL) != 0) {
    LOG_WARN("[ZORRO] Serial echo PTY not available (openpty failed).\n");
    st->pty_master = -1;
    st->pty_slave = -1;
    return;
  }
  st->rx_thread_running = 1;
  if (pthread_create(&st->rx_thread, NULL, z2_serial_rx_thread, st) != 0) {
    LOG_WARN("[ZORRO] Serial echo PTY thread failed.\n");
    st->rx_thread_running = 0;
    close(st->pty_master);
    close(st->pty_slave);
    st->pty_master = -1;
    st->pty_slave = -1;
    return;
  }
  LOG_INFO("[ZORRO] Serial echo host PTY: %s\n", st->pty_name);

  char runtime_dir[256];
  const char *env = getenv("XDG_RUNTIME_DIR");
  if (env && env[0] != '\0') {
    snprintf(runtime_dir, sizeof(runtime_dir), "%s", env);
  } else {
    snprintf(runtime_dir, sizeof(runtime_dir), "/run/user/%u", (unsigned)getuid());
    struct stat st_dir;
    if (stat(runtime_dir, &st_dir) != 0 || !S_ISDIR(st_dir.st_mode)) {
      strlcpy(runtime_dir, "/tmp", sizeof(runtime_dir));
    }
  }

  char serial_dir[256];
  snprintf(serial_dir, sizeof(serial_dir), "%s/amiga/serial", runtime_dir);
  if (mkdir(serial_dir, 0700) != 0 && errno != EEXIST) {
    // try fallback /tmp if runtime path failed
    strlcpy(serial_dir, "/tmp/amiga/serial", sizeof(serial_dir));
    if (mkdir("/tmp/amiga", 0700) != 0 && errno != EEXIST) {
      LOG_WARN("[ZORRO] Unable to create /tmp/amiga: %s\n", strerror(errno));
      return;
    }
    if (mkdir(serial_dir, 0700) != 0 && errno != EEXIST) {
      LOG_WARN("[ZORRO] Unable to create %s: %s\n", serial_dir, strerror(errno));
      return;
    }
  }

  char link_path[256];
  snprintf(link_path, sizeof(link_path), "%s/z2serial0", serial_dir);
  if (unlink(link_path) != 0 && errno != ENOENT) {
    LOG_WARN("[ZORRO] Unable to remove old %s: %s\n", link_path, strerror(errno));
  }
  if (symlink(st->pty_name, link_path) != 0) {
    LOG_WARN("[ZORRO] Unable to symlink %s -> %s: %s\n", link_path, st->pty_name,
             strerror(errno));
    return;
  }
  LOG_INFO("[ZORRO] Serial echo device: %s\n", link_path);
}

static uint8_t z2_serial_read8(zorro_device_t *dev, uint32_t offset) {
  z2_serial_state_t *st = (z2_serial_state_t *)dev->priv;
  switch (offset) {
  case REG_DATA: {
    uint8_t val = 0;
    if (st->head != st->tail) {
      val = st->buf[st->tail];
      st->tail = (uint16_t)((st->tail + 1u) % RX_BUF_SIZE);
    }
    return val;
  }
  case REG_STATUS: {
    uint8_t status = STATUS_TX_READY;
    if (st->head != st->tail) {
      status |= STATUS_RX_READY;
    }
    return status;
  }
  default:
    return 0xFF;
  }
}

static void z2_serial_write8(zorro_device_t *dev, uint32_t offset, uint8_t value) {
  z2_serial_state_t *st = (z2_serial_state_t *)dev->priv;
  switch (offset) {
  case REG_DATA: {
    z2_serial_rx_enqueue(st, value);
    if (st->pty_master >= 0) {
      (void)write(st->pty_master, &value, 1);
    }
    break;
  }
  case REG_CTRL:
    if (value & 0x01u) {
      st->head = 0;
      st->tail = 0;
    }
    break;
  default:
    break;
  }
}

static void z2_serial_reset(zorro_device_t *dev) {
  z2_serial_state_t *st = (z2_serial_state_t *)dev->priv;
  st->head = 0;
  st->tail = 0;
}

static uint8_t z2_serial_rom[] = {
    Z2_Z2,
    AC_MEM_SIZE_64KB,
    0x1,
    0x0,
    0x0,
    0x0,
    0x0,
    0x0,
    PISTORM_AC_MANUF_ID,
    0x0,
    0x0,
    0x0,
    0x0,
    0x0,
    0x0,
    0x0,
    0x0,
    0x0,
    0x0,
    0x0,
};

static zorro_device_t z2_serial_device = {
    .name = "z2-serial-echo",
    .bus = ZORRO_BUS_Z2,
    .size = Z2_SERIAL_SIZE,
    .manufacturer = 0x07DB,
    .product = 0x0010,
    .flags = 0,
    .ac_rom = z2_serial_rom,
    .ac_rom_size = sizeof(z2_serial_rom),
    .reset = z2_serial_reset,
    .read8 = z2_serial_read8,
    .write8 = z2_serial_write8,
    .priv = &serial_state,
};

void z2_serial_echo_register(void) {
  LOG_INFO("[ZORRO] Registering Z2 serial echo device.\n");
  z2_serial_host_init(&serial_state);
  int slot = zorro_register_device(&z2_serial_device);
  if (slot < 0) {
    LOG_INFO("[ZORRO] Failed to register Z2 serial echo device.\n");
  }
}
