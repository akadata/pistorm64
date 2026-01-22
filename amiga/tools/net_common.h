#ifndef AMIGA_TOOLS_NET_COMMON_H
#define AMIGA_TOOLS_NET_COMMON_H

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#define MAX_INTERFACE_ENTRIES 16

struct interface_info {
  char name[IFNAMSIZ];
  struct in_addr addr;
  struct in_addr mask;
};

static int gather_interfaces(int sock, struct interface_info* out, int max_entries) {
  struct ifreq ifr[MAX_INTERFACE_ENTRIES];
  struct ifconf ifc = {
      .ifc_len = sizeof(ifr),
      .ifc_buf = (char*)ifr,
  };

  if (ioctl(sock, SIOCGIFCONF, &ifc) < 0) {
    return -1;
  }

  int count = ifc.ifc_len / sizeof(struct ifreq);
  int added = 0;

  for (int i = 0; i < count && added < max_entries; i++) {
    struct ifreq* req = &ifr[i];
    struct ifreq flags = *req;
    if (ioctl(sock, SIOCGIFFLAGS, &flags) < 0) {
      continue;
    }
    if (!(flags.ifr_flags & IFF_UP) || (flags.ifr_flags & IFF_LOOPBACK)) {
      continue;
    }

    struct sockaddr_in* sin = (struct sockaddr_in*)&req->ifr_addr;
    if (sin->sin_family != AF_INET) {
      continue;
    }

    struct ifreq maskreq = *req;
    if (ioctl(sock, SIOCGIFNETMASK, &maskreq) < 0) {
      continue;
    }

    struct sockaddr_in* mask = (struct sockaddr_in*)&maskreq.ifr_addr;

    struct interface_info* info = &out[added++];
    strncpy(info->name, (const char*)req->ifr_name, IFNAMSIZ);
    info->name[IFNAMSIZ - 1] = '\0';
    info->addr = sin->sin_addr;
    info->mask = mask->sin_addr;
  }

  return added;
}

#endif /* AMIGA_TOOLS_NET_COMMON_H */
