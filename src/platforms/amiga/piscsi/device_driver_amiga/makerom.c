// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define BOOTLDR_SIZE 0x1000
#define DIAG_TOTAL_SIZE 0x4000

#define HUNK_UNIT 0x000003E7u
#define HUNK_NAME 0x000003E8u
#define HUNK_CODE 0x000003E9u

char *rombuf, *zerobuf, *devicebuf;

static uint32_t be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int skip_hunk_name(const uint8_t *buf, uint32_t len, uint32_t *pos) {
  if (*pos + 4 > len) return -1;
  uint32_t longs = be32(buf + *pos);
  *pos += 4;
  if (longs > (len - *pos) / 4) return -1;
  *pos += longs * 4;
  return 0;
}

static int extract_hunk_code(const uint8_t *buf, uint32_t len, uint8_t **out, uint32_t *out_len) {
  uint32_t pos = 0;
  if (len < 8) return -1;

  uint32_t id = be32(buf + pos);
  if (id == HUNK_UNIT) {
    pos += 4;
    if (skip_hunk_name(buf, len, &pos) != 0) return -1;
    while (pos + 4 <= len && be32(buf + pos) == HUNK_NAME) {
      pos += 4;
      if (skip_hunk_name(buf, len, &pos) != 0) return -1;
    }
    if (pos + 4 > len || be32(buf + pos) != HUNK_CODE) return -1;
  } else if (id != HUNK_CODE) {
    return -1;
  }

  pos += 4;
  if (pos + 4 > len) return -1;
  uint32_t code_longs = be32(buf + pos);
  pos += 4;
  if (code_longs > (len - pos) / 4) return -1;

  *out_len = code_longs * 4;
  *out = malloc(*out_len);
  if (!*out) return -1;
  memcpy(*out, buf + pos, *out_len);
  return 0;
}

int main(int argc, char* argv[]) {
  FILE* rom = fopen("bootrom", "rb");
  if (!rom) {
    printf("Could not open file bootrom for reading.\n");
    return 1;
  }
  FILE* out = fopen("../piscsi.rom", "wb+");
  if (!out) {
    printf("Could not open file piscsi.rom for writing.\n");
    fclose(rom);
    return 1;
  }
  FILE* device = NULL;
  if (argc > 1) {
    device = fopen(argv[1], "rb");
  } else {
    device = fopen("pi-scsi.device", "rb");
  }
  if (!device) {
    printf("Could not open device file for reading.\n");
    fclose(rom);
    fclose(out);
    return 1;
  }

  fseek(device, 0, SEEK_END);
  fseek(rom, 0, SEEK_END);
  uint32_t rom_size = (uint32_t)ftell(rom);
  uint32_t device_size = (uint32_t)ftell(device);
  fseek(rom, 0, SEEK_SET);
  fseek(device, 0, SEEK_SET);

  rombuf = malloc(rom_size);
  devicebuf = malloc(device_size);
  if (!rombuf || !devicebuf) {
    printf("Out of memory while allocating buffers.\n");
    fclose(rom);
    fclose(device);
    fclose(out);
    free(rombuf);
    free(devicebuf);
    return 1;
  }

  fread(rombuf, rom_size, 1, rom);
  fread(devicebuf, device_size, 1, device);

  // If bootrom is a HUNK object, extract the CODE payload automatically.
  {
    uint8_t *boot_payload = NULL;
    uint32_t boot_payload_size = 0;
    if (extract_hunk_code((const uint8_t *)rombuf, rom_size, &boot_payload, &boot_payload_size) == 0) {
      free(rombuf);
      rombuf = (char *)boot_payload;
      rom_size = boot_payload_size;
      printf("Detected HUNK bootrom, extracted CODE payload (%u bytes).\n", rom_size);
    }
  }

  if (rom_size > BOOTLDR_SIZE) {
    printf("Boot ROM payload (%u bytes) exceeds boot loader area (%u bytes).\n",
           rom_size, BOOTLDR_SIZE);
    fclose(rom);
    fclose(device);
    fclose(out);
    free(rombuf);
    free(devicebuf);
    return 1;
  }

  uint32_t pad_size = BOOTLDR_SIZE - rom_size;
  zerobuf = malloc(pad_size);
  if (!zerobuf) {
    printf("Out of memory while allocating zero pad.\n");
    fclose(rom);
    fclose(device);
    fclose(out);
    free(rombuf);
    free(devicebuf);
    return 1;
  }
  memset(zerobuf, 0x00, pad_size);

  if (rom_size + pad_size + device_size > DIAG_TOTAL_SIZE) {
    printf("Combined image too large for PiSCSI ROM: boot=%u pad=%u device=%u total=%u limit=%u.\n",
           rom_size, pad_size, device_size, rom_size + pad_size + device_size, DIAG_TOTAL_SIZE);
    fclose(rom);
    fclose(device);
    fclose(out);
    free(rombuf);
    free(devicebuf);
    free(zerobuf);
    return 1;
  }

  fwrite(rombuf, rom_size, 1, out);
  fwrite(zerobuf, pad_size, 1, out);
  fwrite(devicebuf, device_size, 1, out);

  free(zerobuf);
  zerobuf = malloc(DIAG_TOTAL_SIZE - (rom_size + pad_size + device_size));
  memset(zerobuf, 0x00, DIAG_TOTAL_SIZE - (rom_size + pad_size + device_size));
  fwrite(zerobuf, DIAG_TOTAL_SIZE - (rom_size + pad_size + device_size), 1, out);

  printf("piscsi.rom successfully created.\n");

  free(rombuf);
  free(zerobuf);
  free(devicebuf);

  fclose(out);
  fclose(device);
  fclose(rom);

  return 0;
}
