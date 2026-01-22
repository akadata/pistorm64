#include <arpa/inet.h>
#include <stdio.h>
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

  printf("Kernel IP routing table\n");
  printf("Destination       Gateway           Genmask          Iface\n");
  for (int i = 0; i < count; i++) {
    uint32_t netbits = ntohl(ifaces[i].addr.s_addr & ifaces[i].mask.s_addr);
    uint32_t maskbits = ntohl(ifaces[i].mask.s_addr);

    struct in_addr dest_addr = { htonl(netbits) };
    struct in_addr mask_addr = { htonl(maskbits) };

    const char* dest = inet_ntoa(dest_addr);
    const char* mask = inet_ntoa(mask_addr);

    printf("%-16s %-16s %-16s %s\n", dest, "0.0.0.0", mask, ifaces[i].name);
  }

  close(sock);
  return 0;
}
