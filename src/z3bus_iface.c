// SPDX-License-Identifier: MIT

#include "z3bus_iface.h"
#include "log.h"

#include "include/uapi/pistorm_z3bus.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int z3bus_fd = -1;

int z3bus_open(void)
{
  if (z3bus_fd >= 0)
    return z3bus_fd;

  z3bus_fd = open("/dev/z3bus", O_RDWR | O_CLOEXEC);
  if (z3bus_fd < 0) {
    LOG_WARN("[Z3BUS] /dev/z3bus not available (%s)\n", strerror(errno));
    return -1;
  }
  LOG_INFO("[Z3BUS] connected to /dev/z3bus\n");
  return z3bus_fd;
}

void z3bus_close(void)
{
  if (z3bus_fd >= 0) {
    close(z3bus_fd);
    z3bus_fd = -1;
  }
}

int z3bus_is_available(void)
{
  return (z3bus_fd >= 0);
}

int z3bus_enum(struct pistorm_z3bus_dev *out_devs, uint32_t max_devs, uint32_t *out_count)
{
  struct pistorm_z3bus_enum req;
  uint32_t i;

  if (!out_devs || !out_count)
    return -1;

  if (z3bus_fd < 0) {
    if (z3bus_open() < 0)
      return -1;
  }

  memset(&req, 0, sizeof(req));
  req.count = max_devs;

  if (ioctl(z3bus_fd, PISTORM_Z3BUS_IOC_ENUM, &req) < 0) {
    LOG_WARN("[Z3BUS] enum ioctl failed (%s)\n", strerror(errno));
    return -1;
  }

  *out_count = req.count;
  for (i = 0; i < req.count && i < max_devs; i++) {
    out_devs[i] = req.devs[i];
  }

  return 0;
}
