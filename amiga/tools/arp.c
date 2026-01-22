#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "net_common.h"

static void print_usage(const char* prog) {
  printf("Usage: %s [IP|HOST]\n", prog);
}

static int resolve_address(const char* token, struct in_addr* out) {
  if (inet_aton(token, out)) {
    return 0;
  }
  struct hostent* he = gethostbyname(token);
  if (he == NULL || he->h_addr_list == NULL) {
    return -1;
  }
  *out = *(struct in_addr*)he->h_addr_list[0];
  return 0;
}

static void dump_arp_entry(int sock, const char* iface, struct in_addr addr) {
  struct arpreq req;
  memset(&req, 0, sizeof(req));

  struct sockaddr_in* sin = (struct sockaddr_in*)&req.arp_pa;
  sin->sin_family = AF_INET;
  sin->sin_addr = addr;

  if (ioctl(sock, SIOCGARP, &req) != 0) {
    return;
  }

  unsigned char* hw = (unsigned char*)req.arp_ha.sa_data;
  char mac[32] = "??:??:??:??:??:??";
  if (req.arp_ha.sa_family == AF_UNSPEC || req.arp_ha.sa_family == 0) {
    strncpy(mac, "00:00:00:00:00:00", sizeof(mac));
  } else {
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", hw[0], hw[1], hw[2], hw[3], hw[4],
             hw[5]);
  }

  const char* ip = inet_ntoa(sin->sin_addr);

  printf("%-16s %s %s\n", ip ? ip : "0.0.0.0", mac, iface);
}

static void scan_interface(int sock, const struct interface_info* info) {
  uint32_t ip = ntohl(info->addr.s_addr);
  uint32_t mask = ntohl(info->mask.s_addr);
  uint32_t base = ip & mask;
  uint32_t host_bits = (~mask) & 0xFFFFFFFF;
  if (host_bits == 0) {
    return;
  }
  uint32_t limit = host_bits;
  if (limit > 255) {
    limit = 255;
  }

  for (uint32_t offset = 1; offset <= limit; offset++) {
    uint32_t target = base + offset;
    struct in_addr addr = { htonl(target) };
    dump_arp_entry(sock, info->name, addr);
  }
}

int main(int argc, char* argv[]) {
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    perror("socket");
    return 1;
  }

  struct interface_info ifaces[MAX_INTERFACE_ENTRIES];
  int count = gather_interfaces(sock, ifaces, MAX_INTERFACE_ENTRIES);
  if (count < 0) {
    perror("gather interfaces");
    close(sock);
    return 1;
  }

  if (argc > 2) {
    print_usage(argv[0]);
    close(sock);
    return 1;
  }

  printf("Address          HWaddress        Interface\n");
  if (argc == 2) {
    struct in_addr addr;
    if (resolve_address(argv[1], &addr) != 0) {
      fprintf(stderr, "Unable to resolve %s\n", argv[1]);
      close(sock);
      return 1;
    }
    dump_arp_entry(sock, ifaces[0].name, addr);
  } else {
    for (int i = 0; i < count; i++) {
      scan_interface(sock, &ifaces[i]);
    }
  }

  close(sock);
  return 0;
}
