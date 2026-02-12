// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define BOOTLDR_SIZE 0x1000
#define DIAG_TOTAL_SIZE 0x4000

static uint32_t read_be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) |
         ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8)  |
         ((uint32_t)p[3]);
}

static int extract_bootrom_payload(const uint8_t *src, uint32_t src_size,
                                   uint8_t **payload_out, uint32_t *payload_size_out) {
  /* Accept either raw ROM bytes or a HUNK file and extract first CODE hunk. */
  if (src_size < 4 || !payload_out || !payload_size_out) {
    return -1;
  }

  if (read_be32(src) != 0x000003e7u &&
      read_be32(src) != 0x000003f3u &&
      read_be32(src) != 0x000003e8u) {
    uint8_t *buf = malloc(src_size);
    if (!buf) {
      return -1;
    }
    memcpy(buf, src, src_size);
    *payload_out = buf;
    *payload_size_out = src_size;
    return 0;
  }

  /* HUNK format: find first HUNK_CODE tag and use its payload. */
  for (uint32_t pos = 0; pos + 8 <= src_size; pos += 4) {
    uint32_t tag = read_be32(src + pos);
    if (tag != 0x000003e9u) { /* HUNK_CODE */
      continue;
    }
    uint32_t code_longs = read_be32(src + pos + 4);
    if (code_longs > 0x3fffffffU) {
      return -1;
    }
    uint32_t code_bytes = code_longs * 4u;
    uint32_t data_pos = pos + 8;
    if (data_pos + code_bytes > src_size) {
      return -1;
    }
    uint8_t *buf = malloc(code_bytes);
    if (!buf) {
      return -1;
    }
    memcpy(buf, src + data_pos, code_bytes);
    *payload_out = buf;
    *payload_size_out = code_bytes;
    return 0;
  }

  return -1;
}

int main(int argc, char* argv[]) {
  FILE* rom = fopen("bootrom64", "rb");
  if (!rom) {
    printf("Could not open file bootrom for reading.\n");
    return 1;
  }
  FILE* out = fopen("../piscsi64.rom", "wb+");
  if (!out) {
    printf("Could not open file piscsi64.rom for writing.\n");
    fclose(rom);
    return 1;
  }
  FILE* device = NULL;
  if (argc > 1) {
    device = fopen(argv[1], "rb");
  } else {
    device = fopen("pi-scsi64.device", "rb");
  }
  if (!device) {
    printf("Could not open device file for reading.\n");
    fclose(rom);
    fclose(out);
    return 1;
  }

  fseek(device, 0, SEEK_END);
  fseek(rom, 0, SEEK_END);
  uint32_t rom_size = ftell(rom);
  uint32_t device_size = ftell(device);
  fseek(rom, 0, SEEK_SET);
  fseek(device, 0, SEEK_SET);

  uint8_t *rom_in = malloc(rom_size);
  uint8_t *rom_payload = NULL;
  uint32_t rom_payload_size = 0;
  uint8_t *devicebuf = malloc(device_size);
  if (!rom_in || !devicebuf) {
    printf("Out of memory.\n");
    fclose(rom);
    fclose(device);
    fclose(out);
    free(rom_in);
    free(devicebuf);
    return 1;
  }
  fread(rom_in, rom_size, 1, rom);
  fread(devicebuf, device_size, 1, device);

  if (extract_bootrom_payload(rom_in, rom_size, &rom_payload, &rom_payload_size) != 0) {
    printf("Failed to parse bootrom64 payload.\n");
    fclose(rom);
    fclose(device);
    fclose(out);
    free(rom_in);
    free(devicebuf);
    return 1;
  }
  free(rom_in);

  if (rom_payload_size > BOOTLDR_SIZE) {
    printf("Boot ROM payload is too large: 0x%X > 0x%X\n", rom_payload_size, BOOTLDR_SIZE);
    fclose(rom);
    fclose(device);
    fclose(out);
    free(rom_payload);
    free(devicebuf);
    return 1;
  }
  uint32_t pad_size = BOOTLDR_SIZE - rom_payload_size;
  uint8_t *zerobuf = malloc(pad_size);
  if (!zerobuf) {
    printf("Out of memory (pad).\n");
    fclose(rom);
    fclose(device);
    fclose(out);
    free(rom_payload);
    free(devicebuf);
    return 1;
  }
  memset(zerobuf, 0x00, pad_size);

  fwrite(rom_payload, rom_payload_size, 1, out);
  fwrite(zerobuf, pad_size, 1, out);
  fwrite(devicebuf, device_size, 1, out);

  free(zerobuf);
  if (rom_payload_size + pad_size + device_size > DIAG_TOTAL_SIZE) {
    printf("Combined ROM image exceeds diagnostic area size.\n");
    fclose(rom);
    fclose(device);
    fclose(out);
    free(rom_payload);
    free(devicebuf);
    return 1;
  }
  uint32_t tail_size = DIAG_TOTAL_SIZE - (rom_payload_size + pad_size + device_size);
  zerobuf = malloc(tail_size);
  if (!zerobuf) {
    printf("Out of memory (tail).\n");
    fclose(rom);
    fclose(device);
    fclose(out);
    free(rom_payload);
    free(devicebuf);
    return 1;
  }
  memset(zerobuf, 0x00, tail_size);
  fwrite(zerobuf, tail_size, 1, out);

  printf("piscsi64.rom successfully created.\n");

  free(rom_payload);
  free(zerobuf);
  free(devicebuf);

  fclose(out);
  fclose(device);
  fclose(rom);

  return 0;
}
