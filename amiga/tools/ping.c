#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define ICMP_PAYLOAD_SIZE 56
#define PING_COUNT 4

static uint16_t checksum(const void* data, size_t len) {
  const uint16_t* ptr = data;
  uint32_t sum = 0;
  while (len > 1) {
    sum += *ptr++;
    len -= 2;
  }
  if (len) {
    sum += *(const uint8_t*)ptr;
  }
  while (sum >> 16) {
    sum = (sum & 0xFFFF) + (sum >> 16);
  }
  return (uint16_t)~sum;
}

static void print_usage(const char* prog) {
  printf("Usage: %s target\n", prog);
}

static int resolve_target(const char* token, struct in_addr* out) {
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

int main(int argc, char* argv[]) {
  if (argc != 2) {
    print_usage(argv[0]);
    return 1;
  }

  struct sockaddr_in addr = {0};
  if (resolve_target(argv[1], &addr.sin_addr) != 0) {
    fprintf(stderr, "Unable to resolve %s\n", argv[1]);
    return 1;
  }
  addr.sin_family = AF_INET;
  char* target = inet_ntoa(addr.sin_addr);

  int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if (sock < 0) {
    perror("socket");
    return 1;
  }

  pid_t pid = getpid() & 0xFFFF;

  printf("PING %s (%s): %d data bytes\n", argv[1], target, ICMP_PAYLOAD_SIZE);

  for (int seq = 1; seq <= PING_COUNT; seq++) {
    struct icmp packet = {0};
    packet.icmp_type = ICMP_ECHO;
    packet.icmp_code = 0;
    packet.icmp_id = htons(pid);
    packet.icmp_seq = htons(seq);
    struct timeval start, end;
    gettimeofday(&start, NULL);
    memcpy(packet.icmp_data, &start, sizeof(start));
    packet.icmp_cksum = 0;
    packet.icmp_cksum = checksum(&packet, sizeof(packet));

    if (sendto(sock, &packet, sizeof(packet), 0, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
      perror("sendto");
      close(sock);
      return 1;
    }

    fd_set set;
    FD_ZERO(&set);
    FD_SET(sock, &set);
    struct timeval timeout = {.tv_sec = 1, .tv_usec = 0};

    if (select(sock + 1, &set, NULL, NULL, &timeout) > 0) {
      uint8_t buffer[1500];
      struct sockaddr_in from = {0};
      socklen_t len = sizeof(from);
      ssize_t received = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&from, &len);
      if (received < 0) {
        perror("recvfrom");
        continue;
      }
      struct ip* iphdr = (struct ip*)buffer;
      size_t ip_header_len = iphdr->ip_hl * 4;
      struct icmp* reply = (struct icmp*)(buffer + ip_header_len);
      if (reply->icmp_type == ICMP_ECHOREPLY && reply->icmp_id == htons(pid)) {
        gettimeofday(&end, NULL);
        double elapsed = (end.tv_sec - start.tv_sec) * 1000.0 +
                         (end.tv_usec - start.tv_usec) / 1000.0;
        printf("%zd bytes from %s: icmp_seq=%d ttl=%d time=%.1f ms\n", received - ip_header_len,
               inet_ntoa(from.sin_addr), ntohs(reply->icmp_seq), iphdr->ip_ttl, elapsed);
      }
    } else {
      printf("Request timeout for icmp_seq=%d\n", seq);
    }

    sleep(1);
  }

  close(sock);
  return 0;
}
