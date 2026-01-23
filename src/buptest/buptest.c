// SPDX-License-Identifier: MIT

#include <assert.h>
#include <dirent.h>
#include <endian.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "src/emulator.h"
#include "src/gpio/ps_protocol.h"

#define SIZE_KILO 1024u
#define SIZE_MEGA (1024u * 1024u)
#define SIZE_GIGA (1024u * 1024u * 1024u)

static void print_usage(const char *progname) {
  printf("Usage: %s [OPTIONS] [SIZE_KB]\n", progname);
  printf("\n");
  printf("Options:\n");
  printf("  -h, --help        Show this help and exit\n");
  printf("  -a, --about       Show information about this tool and exit\n");
  printf("\n");
  printf("SIZE_KB is the amount of memory to test in kilobytes (default: 512).\n");
  printf("Warning: this tool writes test patterns into the selected address range. Only use it on\n");
  printf("regions that are safe to overwrite (e.g. Chip RAM while the Amiga OS is not relying\n");
  printf("on that memory).\n");
}

static void print_about(const char *progname) {
  printf("%s exercises the PiStorm memory bus via ps_protocol and checks for transaction errors\n",
         progname);
  printf("using read8/read16/read32 and their unaligned variants.\n");
  printf("It is primarily intended for testing Chip RAM ranges on Amiga machines, and the default\n");
  printf("512 KB corresponds to a conservative lower memory window.\n");
  printf("\n");
  printf("Legacy commands are also available: \"%s dumpkick\" dumps 512 KB of Kickstart ROM from\n",
         progname);
  printf("$F80000 to kick.rom (even if your Kickstart is 256 KB), and \"%s dump\" reads 16 MB of\n",
         progname);
  printf("address space to dump.bin.\n");
}

static uint16_t load_u16(const uint8_t* data) {
  uint16_t value = 0;
  memcpy(&value, data, sizeof(value));
  return value;
}

static uint32_t load_u32(const uint8_t* data) {
  uint32_t value = 0;
  memcpy(&value, data, sizeof(value));
  return value;
}

static unsigned int addr_from_size(size_t addr) {
  return (unsigned int)addr;
}

void m68k_set_irq(unsigned int level);

uint8_t* garbege_datas;
struct timespec f2;

uint8_t gayle_int;
int mem_fd;
uint32_t errors = 0;
uint8_t loop_tests = 0;
uint8_t total_errors = 0;

static void sigint_handler(int sig_num) {
  printf("Received sigint %d, exiting.\n", sig_num);
  printf("Total number of transaction errors occured: %u\n",
         (unsigned int)total_errors);
  //ps_cleanup_protocol();
  if (mem_fd) {
    close(mem_fd);
  }
  exit(0);
}

static int wait_txn_idle(const char* tag, int timeout_us) {
  while (timeout_us > 0) {
    if (!(ps_gpio_lev() & (1u << PIN_TXN_IN_PROGRESS))) {
      return 0;
    }
    usleep(10u);
    timeout_us -= 10;
  }
  printf("[RST] Warning: TXN_IN_PROGRESS still set after reset (%s)\n", tag);
  return -1;
}

static void warmup_bus(void) {
  for (int i = 0; i < 64; i++) {
    (void)ps_read_status_reg();
    if ((i & 0x0f) == 0) {
      usleep(100u);
    }
  }
}

static void reset_amiga(const char* tag) {
  for (int attempt = 0; attempt < 3; attempt++) {
    ps_reset_state_machine();
    ps_pulse_reset();
    usleep(1500u);
    if (wait_txn_idle(tag, 20000) == 0) {
      warmup_bus();
      return;
    }
    usleep(2000u);
  }
}

static void __attribute__((unused)) ps_reinit(void) {
  reset_amiga("reinit");

  write8(0xbfe201u, 0x0101u); // CIA OVL
  write8(0xbfe001u, 0x0000u); // CIA OVL LOW
}

static unsigned int dump_read_8(unsigned int address) {
  return ps_read_8(address);
}

static int check_emulator(void) {

  DIR* dir;
  struct dirent* ent;
  char buf[512];

  long pid;
  char pname[100] = {
      0,
  };
  char state;
  FILE* fp = NULL;
  const char* name = "emulator";

  if (!(dir = opendir("/proc"))) {
    perror("can't open /proc, assuming emulator running");
    return 1;
  }

  while ((ent = readdir(dir)) != NULL) {
    long lpid = atol(ent->d_name);
    if (lpid < 0) {
      continue;
    }
    snprintf(buf, sizeof(buf), "/proc/%ld/stat", lpid);
    fp = fopen(buf, "r");

    if (fp) {
      if ((fscanf(fp, "%ld (%99[^)]) %c", &pid, pname, &state)) != 3) {
        printf("fscanf failed, assuming emulator running\n");
        fclose(fp);
        closedir(dir);
        return 1;
      }
      if (!strcmp(pname, name)) {
        fclose(fp);
        closedir(dir);
        return 1;
      }
      fclose(fp);
    }
  }

  closedir(dir);
  return 0;
}

int main(int argc, char* argv[]) {
  const char* progname =
      (argc > 0 && argv[0] != NULL && argv[0][0] != '\0') ? argv[0] : "buptest";
  size_t test_size = 512u * SIZE_KILO;
  uint32_t cur_loop = 0u;
  const char* size_arg = NULL;
  int end_of_options = 0;

  for (int i = 1; i < argc; i++) {
    const char* arg = argv[i];

    if (!end_of_options && arg[0] == '-' && arg[1] != '\0') {
      if (strcmp(arg, "--") == 0) {
        end_of_options = 1;
        continue;
      }
      if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
        print_usage(progname);
        return 0;
      }
      if (strcmp(arg, "-a") == 0 || strcmp(arg, "--about") == 0) {
        print_about(progname);
        return 0;
      }
      fprintf(stderr, "%s: unknown option '%s'\n", progname, arg);
      print_usage(progname);
      return 1;
    }

    if (size_arg != NULL) {
      fprintf(stderr, "%s: too many arguments\n", progname);
      print_usage(progname);
      return 1;
    }
    size_arg = arg;
  }

  if (check_emulator()) {
    printf("PiStorm emulator running, please stop this before running buptest\n");
    return 1;
  }
  clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &f2);
  srand((unsigned int)f2.tv_nsec);

  signal(SIGINT, sigint_handler);

  ps_setup_protocol();
  reset_amiga("startup");

  write8(0xbfe201u, 0x0101u); // CIA OVL
  write8(0xbfe001u, 0x0000u); // CIA OVL LOW

  if (size_arg != NULL) {
    if (strcmp(size_arg, "dumpkick") == 0) {
      printf("Dumping onboard Amiga kickstart from $F80000 to file kick.rom.\n");
      printf("Note that this will always dump 512KB of data, even if your Kickstart is 256KB.\n");
      FILE* out = fopen("kick.rom", "wb+");
      if (out == NULL) {
        printf("Failed to open kick.rom for writing.\nKickstart has not been dumped.\n");
        return 1;
      }

      for (size_t i = 0; i < 512u * SIZE_KILO; i++) {
        unsigned char in = (unsigned char)read8(0xF80000u + addr_from_size(i));
        fputc(in, out);
      }

      fclose(out);
      printf("Amiga Kickstart ROM dumped.\n");
      return 0;
    }

    if (strcmp(size_arg, "dump") == 0) {
      printf("Dumping EVERYTHING to dump.bin.\n");
      FILE* out = fopen("dump.bin", "wb+");
      if (out == NULL) {
        printf("Failed to open dump.bin for writing.\nEverything has not been dumped.\n");
        return 1;
      }

      for (size_t i = 0; i < 16u * SIZE_MEGA; i++) {
        unsigned char in = (unsigned char)dump_read_8(addr_from_size(i));
        fputc(in, out);
        if ((i % 1024u) == 0u) {
          printf(".");
        }
      }
      printf("\n");

      fclose(out);
      printf("Dumped everything.\n");
      return 0;
    }

    int input_kb = atoi(size_arg);
    if (input_kb > 0) {
      test_size = (size_t)input_kb * SIZE_KILO;
    } else {
      test_size = 0u;
    }
    if (test_size == 0 || test_size > 2u * SIZE_MEGA) {
      test_size = 512u * SIZE_KILO;
    }
    printf("Testing %zu KB of memory.\n", test_size / SIZE_KILO);
  }

  garbege_datas = malloc(test_size);
  if (!garbege_datas) {
    printf("Failed to allocate memory for garbege datas.\n");
    return 1;
  }

test_loop:;
  printf("Writing garbege datas.\n");
  for (size_t i = 0; i < test_size; i++) {
    while (garbege_datas[i] == 0x00) {
      garbege_datas[i] = (uint8_t)(rand() % 0xFF);
    }
    write8(addr_from_size(i), (unsigned int)garbege_datas[i]);
  }

  printf("Reading back garbege datas, read8()...\n");
  for (size_t i = 0; i < test_size; i++) {
    uint8_t c = (uint8_t)read8(addr_from_size(i));
    if (c != garbege_datas[i]) {
      if (errors < 512u) {
        printf("READ8: Garbege data mismatch at $%06zX: %02X should be %02X.\n", i,
               (unsigned int)c, (unsigned int)garbege_datas[i]);
      }
      errors++;
    }
  }
  printf("read8 errors total: %" PRIu32 ".\n", errors);
  total_errors = (uint8_t)(total_errors + errors);
  errors = 0;
  sleep(1u);

  printf("Reading back garbege datas, read16(), even addresses...\n");
  for (size_t i = 0; i < test_size - 2u; i += 2u) {
    uint16_t expected = load_u16(&garbege_datas[i]);
    uint16_t c = be16toh((uint16_t)read16(addr_from_size(i)));
    if (c != expected) {
      if (errors < 512u) {
        printf("READ16_EVEN: Garbege data mismatch at $%06zX: %04X should be %04X.\n", i,
               (unsigned int)c, (unsigned int)expected);
      }
      errors++;
    }
  }
  printf("read16 even errors total: %" PRIu32 ".\n", errors);
  total_errors = (uint8_t)(total_errors + errors);
  errors = 0;
  sleep(1u);

  printf("Reading back garbege datas, read16(), odd addresses...\n");
  for (size_t i = 1u; i < test_size - 2u; i += 2u) {
    uint16_t expected = load_u16(&garbege_datas[i]);
    uint16_t raw = (uint16_t)(((uint32_t)read8(addr_from_size(i)) << 8) |
                              (uint32_t)read8(addr_from_size(i + 1u)));
    uint16_t c = be16toh(raw);
    if (c != expected) {
      if (errors < 512u) {
        printf("READ16_ODD: Garbege data mismatch at $%06zX: %04X should be %04X.\n", i,
               (unsigned int)c, (unsigned int)expected);
      }
      errors++;
    }
  }
  printf("read16 odd errors total: %" PRIu32 ".\n", errors);
  errors = 0;
  sleep(1u);

  printf("Reading back garbege datas, read32(), even addresses...\n");
  for (size_t i = 0; i < test_size - 4u; i += 2u) {
    uint32_t expected = load_u32(&garbege_datas[i]);
    uint32_t c = be32toh((uint32_t)read32(addr_from_size(i)));
    if (c != expected) {
      if (errors < 512u) {
        printf("READ32_EVEN: Garbege data mismatch at $%06zX: %08" PRIX32 " should be %08" PRIX32
               ".\n",
               i, c, expected);
      }
      errors++;
    }
  }
  printf("read32 even errors total: %" PRIu32 ".\n", errors);
  total_errors = (uint8_t)(total_errors + errors);
  errors = 0;
  sleep(1u);

  printf("Reading back garbege datas, read32(), odd addresses...\n");
  for (size_t i = 1u; i < test_size - 4u; i += 2u) {
    uint32_t expected = load_u32(&garbege_datas[i]);
    uint32_t c = (uint32_t)read8(addr_from_size(i));
    c |= (uint32_t)be16toh((uint16_t)read16(addr_from_size(i + 1u))) << 8;
    c |= (uint32_t)read8(addr_from_size(i + 3u)) << 24;
    if (c != expected) {
      if (errors < 512u) {
        printf("READ32_ODD: Garbege data mismatch at $%06zX: %08" PRIX32 " should be %08" PRIX32
               ".\n",
               i, c, expected);
      }
      errors++;
    }
  }
  printf("read32 odd errors total: %" PRIu32 ".\n", errors);
  total_errors = (uint8_t)(total_errors + errors);
  errors = 0;
  sleep(1u);

  printf("Clearing %zu KB of Chip again\n", test_size / SIZE_KILO);
  for (size_t i = 0; i < test_size; i++) {
    write8(addr_from_size(i), 0u);
  }

  printf("[WORD] Writing garbege datas to Chip, unaligned...\n");
  for (size_t i = 1u; i < test_size - 2u; i += 2u) {
    uint16_t v = load_u16(&garbege_datas[i]);
    write8(addr_from_size(i), (unsigned int)(v & 0x00FFu));
    write8(addr_from_size(i + 1u), (unsigned int)(v >> 8));
  }

  sleep(1u);
  printf("Reading back garbege datas, read16(), odd addresses...\n");
  for (size_t i = 1u; i < test_size - 2u; i += 2u) {
    uint16_t expected = load_u16(&garbege_datas[i]);
    uint16_t raw = (uint16_t)(((uint32_t)read8(addr_from_size(i)) << 8) |
                              (uint32_t)read8(addr_from_size(i + 1u)));
    uint16_t c = be16toh(raw);
    if (c != expected) {
      if (errors < 512u) {
        printf("READ16_ODD: Garbege data mismatch at $%06zX: %04X should be %04X.\n", i,
               (unsigned int)c, (unsigned int)expected);
      }
      errors++;
    }
  }
  printf("read16 odd errors total: %" PRIu32 ".\n", errors);
  total_errors = (uint8_t)(total_errors + errors);
  errors = 0;

  printf("Clearing %zu KB of Chip again\n", test_size / SIZE_KILO);
  for (size_t i = 0; i < test_size; i++) {
    write8(addr_from_size(i), 0u);
  }

  printf("[LONG] Writing garbege datas to Chip, unaligned...\n");
  for (size_t i = 1u; i < test_size - 4u; i += 4u) {
    uint32_t v = load_u32(&garbege_datas[i]);
    uint16_t mid = (uint16_t)((v & 0x00FFFF00u) >> 8);
    write8(addr_from_size(i), (unsigned int)(v & 0x000000FFu));
    write16(addr_from_size(i + 1u), (unsigned int)htobe16(mid));
    write8(addr_from_size(i + 3u), (unsigned int)((v >> 24) & 0xFFu));
  }

  sleep(1u);
  printf("Reading back garbege datas, read32(), odd addresses...\n");
  for (size_t i = 1u; i < test_size - 4u; i += 4u) {
    uint32_t expected = load_u32(&garbege_datas[i]);
    uint32_t c = (uint32_t)read8(addr_from_size(i));
    c |= (uint32_t)be16toh((uint16_t)read16(addr_from_size(i + 1u))) << 8;
    c |= (uint32_t)read8(addr_from_size(i + 3u)) << 24;
    if (c != expected) {
      if (errors < 512u) {
        printf("READ32_ODD: Garbege data mismatch at $%06zX: %08" PRIX32 " should be %08" PRIX32
               ".\n",
               i, c, expected);
      }
      errors++;
    }
  }
  printf("read32 odd errors total: %" PRIu32 ".\n", errors);
  total_errors = (uint8_t)(total_errors + errors);
  errors = 0;

  if (loop_tests) {
    printf("Loop %" PRIu32 " done. Begin loop %" PRIu32 ".\n", cur_loop + 1u, cur_loop + 2u);
    printf("Current total errors: %u.\n", (unsigned int)total_errors);
    goto test_loop;
  }

  return 0;
}

void m68k_set_irq(unsigned int level __attribute__((unused))) {
}
