//
//  Silly Display Cloner
//  Shows the PI Framebuffer on AMIGA OCS video
//

#include "gpio/ps_protocol.h"
#include <bcm_host.h>
#include <fcntl.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <syslog.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <string.h>

// Some important Amiga Chipset Registers
#define VPOSR 0xDFF004
#define INTREQR 0xDFF01E
#define DSKPTH 0xDFF020
#define DSKLEN 0xDFF024
#define LISAID 0xDFF07C
#define COP1LCH 0xDFF080
#define COP1LCL 0xDFF082
#define COPJMP1 0xDFF088
#define DIWSTRT 0xDFF08E
#define DIWSTOP 0xDFF090
#define DDFSTRT 0xDFF092
#define DDFSTOP 0xDFF094
#define DMACON 0xDFF096
#define INTENA 0xDFF09A
#define INTREQ 0xDFF09C
#define ADKCON 0xDFF09E
#define BPLCON0 0xDFF100
#define BPLCON1 0xDFF102
#define BPLCON2 0xDFF104
#define BPL1MOD 0xDFF108
#define BPL2MOD 0xDFF10A
#define COLOR0 0xDFF180
#define COLOR1 0xDFF182
#define COLOR2 0xDFF184
#define COLOR3 0xDFF186
#define COLOR4 0xDFF188
#define COLOR5 0xDFF18A
#define COLOR6 0xDFF18C
#define COLOR7 0xDFF18E
#define COLOR8 0xDFF190
#define COLOR9 0xDFF192
#define COLOR10 0xDFF194
#define COLOR11 0xDFF196
#define COLOR12 0xDFF198
#define COLOR13 0xDFF19A
#define COLOR14 0xDFF19C
#define COLOR15 0xDFF19E

#define BPL1PTH 0xdff0e0
#define BPL1PTL 0xdff0e2
#define BPL2PTH 0xdff0e4
#define BPL2PTL 0xdff0e6
#define BPL3PTH 0xdff0e8
#define BPL3PTL 0xdff0eA
#define BPL4PTH 0xdff0eC
#define BPL4PTL 0xdff0eE
#define BPL5PTH 0xdff0F0
#define BPL5PTL 0xdff0F2
#define BPL6PTH 0xdff0F4
#define BPL6PTL 0xdff0F6

#define COPBASE 0x0
#define BP1BASE 0x1000
#define BP2BASE BP1BASE + 0x28
#define BP3BASE BP2BASE + 0x28
#define BP4BASE BP3BASE + 0x28
#define BP5BASE BP4BASE + 0x28
#define BP6BASE BP5BASE + 0x28

// volatile unsigned int val;
unsigned char fbdata[614400];
unsigned char fbdata2[153600];
unsigned char planar[153600];
unsigned char eight[153600];

static volatile sig_atomic_t stop = 0;
static int use_image = 0;
static int image_converted = 0;
static char image_path[512];

static void sig_handler(int sig_num) {
  (void)sig_num;
  stop = 1;
}

static int read_token(FILE *fp, char *out, size_t out_len) {
  int ch;
  size_t n = 0;

  while ((ch = fgetc(fp)) != EOF) {
    if (ch == '#') {
      while ((ch = fgetc(fp)) != EOF && ch != '\n') {
      }
      continue;
    }
    if (ch > ' ') {
      ungetc(ch, fp);
      break;
    }
  }

  while ((ch = fgetc(fp)) != EOF && ch > ' ') {
    if (n + 1 < out_len) {
      out[n++] = (char)ch;
    }
  }
  if (n == 0) {
    return -1;
  }
  out[n] = '\0';
  return 0;
}

static int load_ppm_rgb565(const char *path, uint16_t *dst, size_t px_count) {
  FILE *fp = fopen(path, "rb");
  if (!fp) {
    perror("open PPM");
    return -1;
  }

  char tok[64];
  if (read_token(fp, tok, sizeof(tok)) < 0 || strcmp(tok, "P6") != 0) {
    fclose(fp);
    return -1;
  }
  if (read_token(fp, tok, sizeof(tok)) < 0) {
    fclose(fp);
    return -1;
  }
  int width = atoi(tok);
  if (read_token(fp, tok, sizeof(tok)) < 0) {
    fclose(fp);
    return -1;
  }
  int height = atoi(tok);
  if (read_token(fp, tok, sizeof(tok)) < 0) {
    fclose(fp);
    return -1;
  }
  int maxval = atoi(tok);
  if (width * height != (int)px_count || maxval <= 0) {
    fprintf(stderr, "PPM size %dx%d does not match %zu pixels\n", width, height, px_count);
    fclose(fp);
    return -1;
  }

  for (size_t i = 0; i < px_count; i++) {
    int r = fgetc(fp);
    int g = fgetc(fp);
    int b = fgetc(fp);
    if (r == EOF || g == EOF || b == EOF) {
      fclose(fp);
      return -1;
    }
    if (maxval != 255) {
      r = r * 255 / maxval;
      g = g * 255 / maxval;
      b = b * 255 / maxval;
    }
    dst[i] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3));
  }

  fclose(fp);
  return 0;
}

static int load_raw_rgb565(const char *path, uint16_t *dst, size_t px_count) {
  FILE *fp = fopen(path, "rb");
  if (!fp) {
    perror("open RGB565");
    return -1;
  }
  size_t need = px_count * 2;
  size_t rd = fread(dst, 1, need, fp);
  fclose(fp);
  if (rd != need) {
    fprintf(stderr, "RGB565 file size %zu does not match %zu bytes\n", rd, need);
    return -1;
  }
  return 0;
}

static int load_image(uint16_t *dst, size_t px_count) {
  FILE *fp = fopen(image_path, "rb");
  if (!fp) {
    perror("open image");
    return -1;
  }
  int c1 = fgetc(fp);
  int c2 = fgetc(fp);
  fclose(fp);
  if (c1 == 'P' && c2 == '6') {
    return load_ppm_rgb565(image_path, dst, px_count);
  }
  return load_raw_rgb565(image_path, dst, px_count);
}

static int check_emulator(void) {
  DIR *dir;
  struct dirent *ent;
  char buf[512];
  long pid;
  char pname[100] = {0};
  char state;
  FILE *fp = NULL;
  const char *name = "emulator";

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
      if ((fscanf(fp, "%ld (%[^)]) %c", &pid, pname, &state)) != 3) {
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
uint8_t RGB565TORGB323(uint16_t COLOR) {

  uint8_t B = (((COLOR)&0x001F) << 3);
  uint8_t G = (((COLOR)&0x07E0) >> 3);
  uint8_t R = (((COLOR)&0xF800) >> 8);

  uint8_t k = 0;
  k = (((R)&0xE0) | ((G >> 3) & 0x18) | ((B >> 5) & 0x7));

  return k;
}

static inline uint8_t rgb565_to_4bit(uint16_t px) {
  uint8_t r = (px >> 11) & 0x1F;
  uint8_t g = (px >> 5) & 0x3F;
  uint8_t b = px & 0x1F;

  r >>= 3;
  g >>= 4;
  b >>= 3;

  return (r << 2) | g;
}

static void c2p(unsigned char *source, unsigned char *dest, unsigned int width,
                unsigned int height, unsigned int planes) {
  unsigned int alignedwidth, x, y, p, bpr, bpl;
  // alignedwidth = (width + 15) & ~15;
  // bpr = alignedwidth / 8;
  bpr = width / 8;
  bpl = bpr * planes;

  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      unsigned int mask = 0x80 >> (x & 7);
      unsigned int offset = x / 8;
      unsigned char chunkypix = source[x];

      for (p = 0; p < planes; p++) {
        if (chunkypix & (1 << p))
          dest[p * bpr + offset] |= mask;
        else
          dest[p * bpr + offset] &= ~mask;
      }
    }

    source += width;
    dest += bpl;
  }
}

int process() {
  DISPMANX_DISPLAY_HANDLE_T display;
  DISPMANX_MODEINFO_T display_info;
  DISPMANX_RESOURCE_HANDLE_T screen_resource;
  VC_IMAGE_TRANSFORM_T transform;
  uint32_t image_prt;
  VC_RECT_T rect1;
  int ret;

  bcm_host_init();

  display = vc_dispmanx_display_open(0);
  if (!display) {
    syslog(LOG_ERR, "Unable to open primary display");
    return -1;
  }
  ret = vc_dispmanx_display_get_info(display, &display_info);
  if (ret) {
    syslog(LOG_ERR, "Unable to get primary display information");
    return -1;
  }
  syslog(LOG_INFO, "Primary display is %d x %d", display_info.width,
         display_info.height);

  screen_resource =
      vc_dispmanx_resource_create(VC_IMAGE_RGB565, 320, 240, &image_prt);
  if (!screen_resource) {
    syslog(LOG_ERR, "Unable to create screen buffer");
    vc_dispmanx_display_close(display);
    return -1;
  }

  vc_dispmanx_rect_set(&rect1, 0, 0, 320, 240);

  while (!stop) {

    if (use_image) {
      if (!image_converted) {
        memset(planar, 0, sizeof(planar));
        memset(eight, 0, sizeof(eight));
        uint16_t *src = (uint16_t *)fbdata;
        for (int i = 0; i < 320 * 240; i++) {
          eight[i] = rgb565_to_4bit(src[i]);
        }
        c2p(eight, planar, 320, 240, 4);
        image_converted = 1;
      }
    } else {
      ret = vc_dispmanx_snapshot(display, screen_resource, 0);
      (void)ret;
      vc_dispmanx_resource_read_data(screen_resource, &rect1, fbdata,
                                     320 * 16 / 8);

      uint16_t *src = (uint16_t *)fbdata;
      for (int i = 0; i < 320 * 240; i++) {
        eight[i] = rgb565_to_4bit(src[i]);
      }
      c2p(eight, planar, 320, 240, 4);
    }

    for (uint32_t i = 0; i < (0x2580 * 4); i += 2)
      write16(BP1BASE + i, (planar[i + 1]) | planar[i] << 8);
    if (use_image) {
      usleep(10000);
    }
  }

  ret = vc_dispmanx_resource_delete(screen_resource);
  vc_dispmanx_display_close(display);
  return ret ? -1 : 0;
}

int main(int argc, char **argv) {

  /*
          const struct sched_param priority = { 99 };

          sched_setscheduler(0, SCHED_FIFO, &priority);
          mlockall(MCL_CURRENT);
  */

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
      strncpy(image_path, argv[++i], sizeof(image_path) - 1);
      use_image = 1;
    } else {
      printf("Usage: %s [-i image.ppm|image.rgb565]\n", argv[0]);
      return 1;
    }
  }

  if (use_image) {
    if (load_image((uint16_t *)fbdata, 320 * 240) < 0) {
      printf("Failed to load image %s\n", image_path);
      return 1;
    }
  } else if (check_emulator()) {
    printf("PiStorm emulator running, please stop this before running fb2ami\n");
    return 1;
  }

  signal(SIGINT, sig_handler);
  signal(SIGTERM, sig_handler);

  ps_setup_protocol();
  atexit(ps_cleanup_protocol);
  ps_reset_state_machine();
  ps_pulse_reset();

  usleep(100000);

  // copper test
  write8(0xbfe201, 0x0101); // CIA OVL
  write8(0xbfe001, 0x0000); // CIA OVL LOW

  write16(DMACON, 0x0000);  // dma stop
  write16(BPLCON0, 0x4000); // lores 1 bitplane
  write16(BPLCON1, 0x0);
  write16(BPLCON2, 0x0);
  write16(BPL1MOD, 40 * 3);
  write16(BPL2MOD, 40 * 3);

  // bitplane and window

#define WIDTH 320
#define HEIGHT 240
#define XSTRT 129
#define XSTOP XSTRT + WIDTH
#define YSTRT 44
#define YSTOP YSTRT + HEIGHT
#define HSTRT 129
#define RES 8 // 8 = lores, 4 = hires

  write16(DDFSTRT, (HSTRT / 2 - RES));                            // starthor
  write16(DDFSTOP, (HSTRT / 2 - RES) + (8 * ((WIDTH / 16) - 1))); // stop hor
  write16(DIWSTRT, XSTRT + (YSTRT * 256));               // start window
  write16(DIWSTOP, (XSTOP - 256) + (YSTOP - 256) * 256); // stop window

  write16(COLOR0,  0x0000); // black
  write16(COLOR1,  0xF000); // red
  write16(COLOR2,  0x0F00); // green
  write16(COLOR3,  0x00F0); // blue
  write16(COLOR4,  0xFF00); // yellow
  write16(COLOR5,  0xF0F0); // magenta
  write16(COLOR6,  0x0FF0); // cyan
  write16(COLOR7,  0xFFF0); // white
  write16(COLOR8,  0x7000); // dark red
  write16(COLOR9,  0x0700); // dark green
  write16(COLOR10, 0x0070); // dark blue
  write16(COLOR11, 0x7700); // olive
  write16(COLOR12, 0x7070); // purple
  write16(COLOR13, 0x0770); // teal
  write16(COLOR14, 0x7770); // light gray
  write16(COLOR15, 0x8880); // gray

  // load copperlist into chipmem
  uint32_t addr = COPBASE; // 0x2000 looks like a fine place for it...

  write16(addr, BPL1PTH);
  addr += 2;
  write16(addr, 0x0000);
  addr += 2; // bitplane pointer
  write16(addr, BPL1PTL);
  addr += 2;
  write16(addr, BP1BASE);
  addr += 2; // bitplane pointer
  write16(addr, BPL2PTH);
  addr += 2;
  write16(addr, 0x0000);
  addr += 2; // bitplane pointer
  write16(addr, BPL2PTL);
  addr += 2;
  write16(addr, BP2BASE);
  addr += 2; // bitplane pointer
  write16(addr, BPL3PTH);
  addr += 2;
  write16(addr, 0x0000);
  addr += 2; // bitplane pointer
  write16(addr, BPL3PTL);
  addr += 2;
  write16(addr, BP3BASE);
  addr += 2; // bitplane pointer
  write16(addr, BPL4PTH);
  addr += 2;
  write16(addr, 0x0000);
  addr += 2; // bitplane pointer
  write16(addr, BPL4PTL);
  addr += 2;
  write16(addr, BP4BASE);
  addr += 2; // bitplane pointer

  write16(addr, 0xFFFF);
  addr += 2;
  write16(addr, 0xFFFE);
  addr += 2; // end of copper list

  write32(COP1LCH, COPBASE);
  write16(COPJMP1, COPBASE); // start copper
  write16(DMACON, 0x8390);   // dma go go go


  process();
  ps_cleanup_protocol();

  return 0;
}

// fb2ami standalone tool does not run a 68k core.
// ps_protocol links against m68k_set_irq() from the emulator, so provide a stub.
void m68k_set_irq(unsigned int level __attribute__((unused))) {
}
