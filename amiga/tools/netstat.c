#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "net_common.h"

static void print_usage(const char* prog) {
  printf("Usage: %s\n", prog);
}

int main(int argc, char* argv[]) {
  if (argc > 1) {
    print_usage(argv[0]);
    return 1;
  }

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

  printf("Iface        Address         Mask            Broadcast\n");
  for (int i = 0; i < count; i++) {
    const char* addr = inet_ntoa(ifaces[i].addr);
    const char* mask = inet_ntoa(ifaces[i].mask);
    char bcast[16] = "0.0.0.0";

    struct ifreq req;
    memset(&req, 0, sizeof(req));
    strncpy((char*)req.ifr_name, ifaces[i].name, IFNAMSIZ);
    if (ioctl(sock, SIOCGIFBRDADDR, &req) == 0) {
    struct sockaddr_in* sin = (struct sockaddr_in*)&req.ifr_broadaddr;
      const char* bc = inet_ntoa(sin->sin_addr);
      if (bc) {
        strncpy(bcast, bc, sizeof(bcast));
        bcast[sizeof(bcast) - 1] = '\0';
      }
    }

    printf("%-12s %-15s %-15s %-15s\n", ifaces[i].name, addr, mask, bcast);
  }

  close(sock);
  return 0;
}
