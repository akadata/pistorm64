// SPDX-License-Identifier: MIT

#include "config_file/config_file.h"
#include "platforms/amiga/pistorm-dev/pistorm-dev-enums.h"
#include "emulator.h"
#include "rtg.h"
#include "log.h"

#include "raylib.h"

#include <dirent.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Helper functions for safe unaligned memory access
static inline uint16_t load_u16_be(const uint8_t *p) {
    uint16_t v;
    memcpy(&v, p, sizeof v);
    return be16toh(v);
}

// Default configuration for VideoCore / TV service support (Raspberry Pi only).
// Makefile can override with -DUSE_VC=1 when vc_tvservice is available.
#ifndef USE_VC
#define USE_VC 0
#endif

#if USE_VC
#include "interface/vmcs_host/vc_tvservice.h"
#endif

#define RTG_INIT_ERR(a) { LOG_ERROR("%s", a); *data->running = 0; }

#define DEBUG_RAYLIB_RTG

#ifdef DEBUG_RAYLIB_RTG
#define DEBUG LOG_DEBUG
#else
#define DEBUG(...)
#endif

uint8_t busy = 0;
uint8_t rtg_on = 0;
uint8_t rtg_initialized = 0;
uint8_t emulator_exiting = 0;
uint8_t rtg_output_in_vblank = 0;
uint8_t rtg_dpms = 0;
uint8_t shutdown = 0;

extern uint8_t *rtg_mem;
extern uint8_t display_enabled;

extern uint32_t framebuffer_addr;
extern uint32_t framebuffer_addr_adj;

extern uint16_t rtg_display_width;
extern uint16_t rtg_display_height;
extern uint16_t rtg_display_format;
extern uint16_t rtg_pitch;
extern uint16_t rtg_total_rows;
extern uint16_t rtg_offset_x;
extern uint16_t rtg_offset_y;

uint32_t cur_rtg_frame = 0;

static pthread_t thread_id;

static uint8_t mouse_cursor_enabled = 0;
static uint8_t cursor_image_updated = 0;
static uint8_t clut_cursor_enabled  = 0;
static uint8_t updating_screen      = 0;
static uint8_t debug_palette        = 0;
static uint8_t show_fps             = 0;
static uint8_t palette_updated      = 0;

static uint16_t mouse_cursor_w      = 16;
static uint16_t mouse_cursor_h      = 16;
static int16_t  mouse_cursor_x      = 0;
static int16_t  mouse_cursor_y      = 0;
static int16_t  mouse_cursor_x_adj  = 0;
static int16_t  mouse_cursor_y_adj  = 0;


static int32_t pi_screen_width      = 1920;
static int32_t pi_screen_height     = 1080;
static uint8_t pi_screen_width_set  = 0;
static uint8_t pi_screen_height_set = 0;

static const size_t rtg_mem_size = 64u * SIZE_MEGA;  // was 40u   we have upto 512mb ram sensibly on a pi4 lets not be stingy

struct rtg_shared_data {
  uint16_t *width;
  uint16_t *height;
  uint16_t *format;
  uint16_t *pitch;
  uint16_t *offset_x;
  uint16_t *offset_y;
  uint8_t* memory;
  uint32_t* addr;
  uint8_t* running;
};

float scale_x = 1.0f;
float scale_y = 1.0f;
// Source rect in RTG buffer coordinates
static Rectangle srcrect;

// Destination rect for raylib scaling/output
static Rectangle dstscale;

static Vector2 origin;
static uint8_t scale_mode = PIGFX_SCALE_FULL;
static uint8_t filter_mode = 0;

struct rtg_shared_data rtg_share_data;
static uint32_t palette[256];
static uint32_t cursor_palette[256];

uint32_t cursor_data[256 * 256];
uint32_t clut_cursor_texture_data[256 * 256];

// Forward declarations for -Wmissing-prototypes
extern void rtg_update_screen(void);
extern void rtg_scale_output(uint16_t width, uint16_t height);
extern void* rtgThread(void* args);
extern void update_mouse_cursor(uint8_t* src);

void rtg_update_screen(void) {
  // doing nothing to update the screen here????
}

static int rtg_read_first_line(const char* path, char* buf, size_t buf_size) {
  FILE* file = fopen(path, "r");
  if(!file) {
    return 0;
  }
  if(!fgets(buf, (int)buf_size, file)) {
    fclose(file);
    return 0;
  }
  fclose(file);
  int len = (int)strcspn(buf, "\r\n");
  buf[len] = '\0';
  return 1;
}

static int rtg_parse_mode_line(const char* line, int* out_w, int* out_h) {
  int w = 0;
  int h = 0;
  if(sscanf(line, "%dx%d", &w, &h) != 2) {
    return 0;
  }
  if(w <= 0 || h <= 0) {
    return 0;
  }
  *out_w = w;
  *out_h = h;
  return 1;
}

static int rtg_detect_screen_size(int req_w, int req_h, int* out_w, int* out_h, char* out_conn,
                                  size_t out_conn_size, int* out_req_supported) {
  const char* base = "/sys/class/drm/";
  const char* status_suffix = "/status";
  const char* modes_suffix = "/modes";
  size_t base_len = strlen(base);
  size_t status_len = strlen(status_suffix);
  size_t modes_len = strlen(modes_suffix);
  DIR* dir = opendir("/sys/class/drm");
  if(!dir) {
    return 0;
  }
  struct dirent* ent = NULL;
  char path[256];
  char line[128];
  if(out_req_supported) {
    *out_req_supported = 0;
  }
  while ((ent = readdir(dir)) != NULL) {
    const char* name = ent->d_name;
    if(name[0] == '.') {
      continue;
    }
    if(strncmp(name, "card", 4) != 0) {
      continue;
    }
    if(strchr(name, '-') == NULL) {
      continue;
    }
    size_t name_len = strnlen(name, sizeof(path));
    if(name_len + base_len + status_len + 1 >= sizeof(path)) {
      continue;
    }
    snprintf(path, sizeof(path), "%s%.*s%s", base, (int)name_len, name, status_suffix);
    if(!rtg_read_first_line(path, line, sizeof(line))) {
      continue;
    }
    if(strcmp(line, "connected") != 0) {
      continue;
    }
    if(name_len + base_len + modes_len + 1 >= sizeof(path)) {
      continue;
    }
    snprintf(path, sizeof(path), "%s%.*s%s", base, (int)name_len, name, modes_suffix);
    FILE* modes = fopen(path, "r");
    if(!modes) {
      continue;
    }
    int pref_w = 0;
    int pref_h = 0;
    int req_ok = 0;
    while (fgets(line, sizeof(line), modes)) {
      int mode_w = 0;
      int mode_h = 0;
      if(!rtg_parse_mode_line(line, &mode_w, &mode_h)) {
        continue;
      }
      if(pref_w == 0 && pref_h == 0) {
        pref_w = mode_w;
        pref_h = mode_h;
      }
      if(req_w > 0 && req_h > 0 && mode_w == req_w && mode_h == req_h) {
        req_ok = 1;
      }
    }
    fclose(modes);
    if(pref_w == 0 || pref_h == 0) {
      continue;
    }
    if(req_ok) {
      *out_w = req_w;
      *out_h = req_h;
      if(out_req_supported) {
        *out_req_supported = 1;
      }
    } else {
      *out_w = pref_w;
      *out_h = pref_h;
    }
    if(out_conn && out_conn_size > 0) {
      size_t out_cap = out_conn_size - 1;
      size_t copy_len = name_len;
      if(copy_len > out_cap) {
        copy_len = out_cap;
      }
      memcpy(out_conn, name, copy_len);
      out_conn[copy_len] = '\0';
    }
    closedir(dir);
    return 1;
  }
  closedir(dir);
  return 0;
}

static void rtg_autodetect_screen_size(void) {
  int req_w = 0;
  int req_h = 0;
  if(pi_screen_width_set && pi_screen_height_set) {
    req_w = pi_screen_width;
    req_h = pi_screen_height;
  }
  int auto_w = 0;
  int auto_h = 0;
  int req_supported = 0;
  char conn[64] = {0};
  if(!rtg_detect_screen_size(req_w, req_h, &auto_w, &auto_h, conn, sizeof(conn), &req_supported)) {
    return;
  }
  if(req_w > 0 && req_h > 0 && !req_supported) {
    LOG_WARN("[RTG/RAYLIB] Output mode %dx%d not available; falling back to %dx%d\n", req_w, req_h,
             auto_w, auto_h);
  }
  pi_screen_width = auto_w;
  pi_screen_height = auto_h;
  if(conn[0] != '\0') {
    LOG_INFO("[RTG/RAYLIB] Auto output mode: %dx%d (%s)\n", pi_screen_width, pi_screen_height,
             conn);
  } else {
    LOG_INFO("[RTG/RAYLIB] Auto output mode: %dx%d\n", pi_screen_width, pi_screen_height);
  }
}

uint32_t rtg_to_raylib[RTGFMT_NUM] = {
    PIXELFORMAT_UNCOMPRESSED_GRAYSCALE, // 4BIT_PLANAR,
    PIXELFORMAT_UNCOMPRESSED_GRAYSCALE, // 8BIT_CLUT,
    PIXELFORMAT_UNCOMPRESSED_R5G6B5,    // RGB565_BE,
    PIXELFORMAT_UNCOMPRESSED_R5G6B5,    // RGB565_LE,
    PIXELFORMAT_UNCOMPRESSED_R5G6B5,    // BGR565_LE,
    PIXELFORMAT_UNCOMPRESSED_R8G8B8,    // RGB24,
    PIXELFORMAT_UNCOMPRESSED_R8G8B8,    // BGR24,
    PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,  // RGB32_ARGB,
    PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,  // RGB32_ABGR,
    PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,  // RGB32_RGBA,
    PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,  // RGB32_BGRA,
    PIXELFORMAT_UNCOMPRESSED_R5G5B5A1,  // RGB555_BE,
    PIXELFORMAT_UNCOMPRESSED_R5G5B5A1,  // RGB555_LE,
    PIXELFORMAT_UNCOMPRESSED_R5G5B5A1,  // BGR555_LE,
    PIXELFORMAT_UNCOMPRESSED_GRAYSCALE, // NONE,
};

static void rtg_copy_tight_rows(uint8_t* dst, const uint8_t* src, size_t row_bytes, size_t pitch,
                                size_t height) {
  for (size_t y = 0; y < height; y++) {
    memcpy(dst + (y * row_bytes), src + (y * pitch), row_bytes);
  }
}

void rtg_scale_output(uint16_t width, uint16_t height) {
  static uint8_t center = 1;
  float screen_w = (float)pi_screen_width;
  float screen_h = (float)pi_screen_height;
  float src_w = (float)width;
  float src_h = (float)height;

  srcrect.x = srcrect.y = 0;
  srcrect.width = src_w;
  srcrect.height = src_h;

  if(scale_mode != PIGFX_SCALE_CUSTOM && scale_mode != PIGFX_SCALE_CUSTOM_RECT) {
    dstscale.x = dstscale.y = 0;
    dstscale.width = src_w;
    dstscale.height = src_h;
    mouse_cursor_x_adj = 0;
    mouse_cursor_y_adj = 0;
    center = 1;
  } else {
    center = 0;
  }

  if(src_w <= 0.0f || src_h <= 0.0f || screen_w <= 0.0f || screen_h <= 0.0f) {
    dstscale.width = 128.0f;
    dstscale.height = 128.0f;
  } else {
    float dst_w = dstscale.width;
    float dst_h = dstscale.height;

    switch (scale_mode) {
    case PIGFX_SCALE_INTEGER_MAX: {
      float scale = floorf(fminf(screen_w / src_w, screen_h / src_h));
      if(scale < 1.0f) {
        scale = 1.0f;
      }
      dst_w = src_w * scale;
      dst_h = src_h * scale;
      break;
    }
    case PIGFX_SCALE_FULL_ASPECT: {
      float scale = fminf(screen_w / src_w, screen_h / src_h);
      dst_w = src_w * scale;
      dst_h = src_h * scale;
      break;
    }
    case PIGFX_SCALE_FULL_43: {
      const float aspect = 4.0f / 3.0f;
      dst_w = screen_w;
      dst_h = screen_w / aspect;
      if(dst_h > screen_h) {
        dst_h = screen_h;
        dst_w = screen_h * aspect;
      }
      break;
    }
    case PIGFX_SCALE_FULL_169: {
      const float aspect = 16.0f / 9.0f;
      dst_w = screen_w;
      dst_h = screen_w / aspect;
      if(dst_h > screen_h) {
        dst_h = screen_h;
        dst_w = screen_h * aspect;
      }
      break;
    }
    case PIGFX_SCALE_FULL:
      dst_w = screen_w;
      dst_h = screen_h;
      break;
    case PIGFX_SCALE_NONE:
      dst_w = src_w;
      dst_h = src_h;
      break;
    case PIGFX_SCALE_CUSTOM:
    case PIGFX_SCALE_CUSTOM_RECT:
    default:
      dst_w = dstscale.width;
      dst_h = dstscale.height;
      break;
    }

    dstscale.width = dst_w;
    dstscale.height = dst_h;
  }

  if(center) {
    dstscale.x = (screen_w - dstscale.width) * 0.5f;
    dstscale.y = (screen_h - dstscale.height) * 0.5f;
  }

  scale_x = (src_w > 0.0f) ? (dstscale.width / src_w) : 1.0f;
  scale_y = (src_h > 0.0f) ? (dstscale.height / src_h) : 1.0f;

  origin.x = 0.0f;
  origin.y = 0.0f;

  DEBUG("[RTG/RAYLIB] Scale mode=%u src=%ux%u dst=%.1fx%.1f offset=%.1f,%.1f\n", scale_mode, width,
        height, dstscale.width, dstscale.height, dstscale.x, dstscale.y);
}

void* rtgThread(void* args) {

  LOG_INFO("[RTG] Thread running\n");

  int reinit = 0;
  int old_filter_mode = -1;
  int force_filter_mode = 0;
  rtg_on = 1;

  uint16_t* indexed_buf = NULL;
  size_t indexed_buf_size = 0;
  uint8_t* tight_buf = NULL;
  size_t tight_buf_size = 0;
  uint32_t last_frame_addr = 0;
  uint32_t frame_no = 0;

  rtg_share_data.format = &rtg_display_format;
  rtg_share_data.width = &rtg_display_width;
  rtg_share_data.height = &rtg_display_height;
  rtg_share_data.pitch = &rtg_pitch;
  rtg_share_data.offset_x = &rtg_offset_x;
  rtg_share_data.offset_y = &rtg_offset_y;
  rtg_share_data.memory = rtg_mem;
  rtg_share_data.running = &rtg_on;
  rtg_share_data.addr = &framebuffer_addr_adj;
  struct rtg_shared_data* data = &rtg_share_data;

  uint16_t width = rtg_display_width;
  uint16_t height = rtg_display_height;
  uint16_t format = rtg_display_format;
  uint16_t pitch = rtg_pitch;

  Texture raylib_texture = {0};
  Texture raylib_cursor_texture = {0};
  Texture raylib_clut_texture = {0};

  Image raylib_fb = {0};
  Image raylib_cursor = {0};
  Image raylib_clut = {0};


  rtg_autodetect_screen_size();
  LOG_INFO("[RTG/RAYLIB] Output resolution: %dx%d\n", pi_screen_width, pi_screen_height);

  InitWindow(pi_screen_width, pi_screen_height, "Pistorm RTG");

  if(!IsWindowReady()) {
    LOG_ERROR("[RTG/RAYLIB] InitWindow failed for %dx%d; disabling RTG output.\n", pi_screen_width,
              pi_screen_height);
    rtg_on = 0;
    rtg_initialized = 0;
    display_enabled = 0xFF;
    return args;
  }
  HideCursor();

  // Enable VSync to synchronize with display refresh rate and reduce tearing
  SetConfigFlags(FLAG_VSYNC_HINT);
  // Set target FPS to 0 to let VSync control the frame rate
  SetTargetFPS(0);

  Color bef = {0, 64, 128, 255};
  Color black = {0, 0, 0, 255};

  Shader clut_shader = LoadShader(NULL, "src/platforms/amiga/rtg/clut.shader");
  Shader bgra_swizzle_shader = LoadShader(NULL, "src/platforms/amiga/rtg/bgraswizzle.shader");
  Shader argb_swizzle_shader = LoadShader(NULL, "src/platforms/amiga/rtg/argbswizzle.shader");
  int clut_loc = GetShaderLocation(clut_shader, "texture1");

  raylib_clut.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
  raylib_clut.width = 256;
  raylib_clut.height = 1;
  raylib_clut.mipmaps = 1;
  raylib_clut.data = palette;

  raylib_clut_texture = LoadTextureFromImage(raylib_clut);
  SetTextureWrap(raylib_clut_texture, TEXTURE_WRAP_CLAMP);

  raylib_cursor.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
  raylib_cursor.width = 256;
  raylib_cursor.height = 256;
  raylib_cursor.mipmaps = 1;
  raylib_cursor.data = cursor_data;
  raylib_cursor_texture = LoadTextureFromImage(raylib_cursor);

reinit_raylib:;
  if(reinit) {
    LOG_INFO("Reinitializing raylib...\n");
    width = rtg_display_width;
    height = rtg_display_height;
    format = rtg_display_format;
    pitch = rtg_pitch;
    if(indexed_buf) {
      free(indexed_buf);
      indexed_buf = NULL;
      indexed_buf_size = 0;
    }
    if(tight_buf) {
      free(tight_buf);
      tight_buf = NULL;
      tight_buf_size = 0;
    }
    UnloadTexture(raylib_texture);
    old_filter_mode = -1;
    reinit = 0;
  }

  LOG_INFO("Creating %dx%d raylib window...\n", width, height);

  LOG_DEBUG("Setting up raylib framebuffer image.\n");
  if(format >= RTGFMT_NUM) {
    LOG_ERROR("[RTG/RAYLIB] Invalid RTG format: %u\n", format);
    reinit = 1;
    goto shutdown_raylib;
  }

  size_t bpp = rtg_pixel_size[format];
  if(width == 0 || height == 0 || bpp == 0) {
    LOG_ERROR("[RTG/RAYLIB] Invalid framebuffer params: %ux%u bpp=%zu format=%u\n", width, height,
              bpp, format);
    reinit = 1;
    goto shutdown_raylib;
  }

  if(width > SIZE_MAX / bpp) {
    LOG_ERROR("[RTG/RAYLIB] Framebuffer width overflow: %u bpp=%zu\n", width, bpp);
    reinit = 1;
    goto shutdown_raylib;
  }
  size_t row_bytes = (size_t)width * bpp;
  if(height > 0 && row_bytes > SIZE_MAX / height) {
    LOG_ERROR("[RTG/RAYLIB] Framebuffer size overflow: row_bytes=%zu height=%u\n", row_bytes,
              height);
    reinit = 1;
    goto shutdown_raylib;
  }
  size_t tight_size = row_bytes * height;

  uint32_t frame_addr = *data->addr;
  size_t addr = (size_t)frame_addr;
  size_t needed = (size_t)pitch * height;
  int pitch_ok = (pitch >= row_bytes);
  int addr_ok = pitch_ok && (addr < rtg_mem_size) && (addr + needed <= rtg_mem_size);

  raylib_fb.format = (int)rtg_to_raylib[format];
  raylib_fb.width = width;
  raylib_fb.height = height;
  raylib_fb.mipmaps = 1;
  raylib_fb.data = NULL;

  if(!addr_ok) {
    LOG_WARN("[RTG/RAYLIB] Framebuffer bounds invalid: addr=0x%08X pitch=%u width=%u height=%u "
             "bpp=%zu needed=%zu\n",
             frame_addr, pitch, width, height, bpp, needed);
    if(tight_buf_size < tight_size) {
      void* resized = realloc(tight_buf, tight_size);
      if(!resized) {
        LOG_ERROR("[RTG/RAYLIB] Failed to allocate tight buffer (%zu bytes)\n", tight_size);
        reinit = 1;
        goto shutdown_raylib;
      }
      tight_buf = resized;
      tight_buf_size = tight_size;
    }
    memset(tight_buf, 0, tight_size);
    raylib_fb.data = tight_buf;
  } else if(format == RTGFMT_RGB565_BE || format == RTGFMT_RGB555_BE) {
    if((pitch % 2) != 0) {
      LOG_WARN("[RTG/RAYLIB] 16-bit pitch not aligned: pitch=%u\n", pitch);
      reinit = 1;
      goto shutdown_raylib;
    }
    if(indexed_buf_size < tight_size) {
      void* resized = realloc(indexed_buf, tight_size);
      if(!resized) {
        LOG_ERROR("[RTG/RAYLIB] Failed to allocate indexed buffer (%zu bytes)\n", tight_size);
        reinit = 1;
        goto shutdown_raylib;
      }
      indexed_buf = resized;
      indexed_buf_size = tight_size;
    }
    size_t src_stride = pitch / 2;
    for (uint16_t y = 0; y < height; y++) {
      for (uint16_t x = 0; x < width; x++) {
        size_t src_idx = (x + (y * src_stride)) * sizeof(uint16_t);
        const uint8_t* src_ptr = data->memory + addr + src_idx;
        indexed_buf[x + (y * width)] = load_u16_be(src_ptr);
      }
    }
    raylib_fb.data = indexed_buf;
  } else if(pitch != row_bytes) {
    if(tight_buf_size < tight_size) {
      void* resized = realloc(tight_buf, tight_size);
      if(!resized) {
        LOG_ERROR("[RTG/RAYLIB] Failed to allocate tight buffer (%zu bytes)\n", tight_size);
        reinit = 1;
        goto shutdown_raylib;
      }
      tight_buf = resized;
      tight_buf_size = tight_size;
    }
    rtg_copy_tight_rows(tight_buf, data->memory + addr, row_bytes, pitch, height);
    raylib_fb.data = tight_buf;
  } else {
    raylib_fb.data = &data->memory[frame_addr];
  }

  LOG_DEBUG("[RTG/RAYLIB] FB init: %ux%u pitch=%u bpp=%zu addr=0x%08X\n", width, height, pitch, bpp,
            frame_addr);
  raylib_texture = LoadTextureFromImage(raylib_fb);
  if(raylib_texture.id == 0) {
    LOG_ERROR("[RTG/RAYLIB] Failed to create framebuffer texture; disabling RTG output.\n");
    rtg_on = 0;
    display_enabled = 0xFF;
    reinit = 0;
    goto shutdown_raylib;
  }
  LOG_DEBUG("Loaded framebuffer texture.\n");

  rtg_scale_output(width, height);
  LOG_DEBUG("rtg_scale_output complete.\n");
  force_filter_mode = 0;

  while (1) {
    if(rtg_on) {
      uint16_t current_width = *data->width;
      uint16_t current_height = *data->height;
      uint16_t current_format = *data->format;
      uint16_t current_pitch = *data->pitch;
      uint32_t current_addr = *data->addr;

      if(current_format >= RTGFMT_NUM) {
        LOG_ERROR("[RTG/RAYLIB] Invalid RTG format during frame update: %u\n", current_format);
        reinit = 1;
        goto shutdown_raylib;
      }

      if(current_width != width || current_height != height || current_format != format ||
          current_pitch != pitch) {
        LOG_INFO("[RTG/RAYLIB] Mode change detected: %ux%u fmt=%u pitch=%u -> %ux%u fmt=%u "
                 "pitch=%u\n",
                 width, height, format, pitch, current_width, current_height, current_format,
                 current_pitch);
        reinit = 1;
        goto shutdown_raylib;
      }

      if(current_addr != last_frame_addr) {
        LOG_DEBUG("[RTG/RAYLIB] FB addr update: 0x%08X\n", current_addr);
        last_frame_addr = current_addr;
      }

      if(frame_no < 3) {
        LOG_DEBUG("[RTG/RAYLIB] Frame %u start: addr=0x%08X mem=%p %ux%u pitch=%u bpp=%zu "
                  "tex_id=%u\n",
                  frame_no, current_addr,
                  (void*)data->memory,
                  width,
                  height,
                  pitch,
                  bpp,
                  raylib_texture.id);
      }

      if(old_filter_mode != filter_mode) {
        old_filter_mode = filter_mode;
        SetTextureFilter(raylib_texture, filter_mode);
        SetTextureFilter(raylib_cursor_texture, filter_mode);
      }
      /* If we are not in 16bit mode then don't use any filtering - otherwise force_filter_mode to
       * no smoothing */
      if(force_filter_mode == 0) {
        if(bpp != 2 && filter_mode != 0) {
          LOG_DEBUG("[RTG/RAYLIB] Disabling smoothing (non-16bpp mode)\n");
          force_filter_mode = 1;
          old_filter_mode = filter_mode;
          SetTextureFilter(raylib_texture, 0);
          SetTextureFilter(raylib_cursor_texture, 0);
        }
      } else {
        if(bpp == 2) {
          LOG_DEBUG("[RTG/RAYLIB] Restoring smoothing (16bpp mode)\n");
          force_filter_mode = 0;
          old_filter_mode = -1;
        }
      }
      BeginDrawing();
      ClearBackground(black);
      rtg_output_in_vblank = 0;
      updating_screen = 1;

      switch (format) {
      case RTGFMT_8BIT_CLUT:
        BeginShaderMode(clut_shader);
        SetShaderValueTexture(clut_shader, clut_loc, raylib_clut_texture);
        break;
      case RTGFMT_RGB32_BGRA:
        BeginShaderMode(bgra_swizzle_shader);
        break;
      case RTGFMT_RGB32_ARGB:
        BeginShaderMode(argb_swizzle_shader);
        break;
      }

      DrawTexturePro(raylib_texture, srcrect, dstscale, origin, 0.0f, RAYWHITE);

      switch (format) {
      case RTGFMT_8BIT_CLUT:
      case RTGFMT_RGB32_BGRA:
      case RTGFMT_RGB32_ARGB:
        EndShaderMode();
        break;
      }

      if(mouse_cursor_enabled || clut_cursor_enabled) {
        float mc_x = mouse_cursor_x - rtg_offset_x + mouse_cursor_x_adj;
        float mc_y = mouse_cursor_y - rtg_offset_y + mouse_cursor_y_adj;
        float cursor_off_x = dstscale.x;
        float cursor_off_y = dstscale.y;

        if(scale_mode == PIGFX_SCALE_CUSTOM || scale_mode == PIGFX_SCALE_CUSTOM_RECT) {
          cursor_off_x = 0.0f;
          cursor_off_y = 0.0f;
        }

        Rectangle cursor_srcrect = {0, 0, mouse_cursor_w, mouse_cursor_h};

        Rectangle dstrect = {cursor_off_x + (mc_x * scale_x), cursor_off_y + (mc_y * scale_y),
                             (float)mouse_cursor_w * scale_x, (float)mouse_cursor_h * scale_y};

        DrawTexturePro(raylib_cursor_texture, cursor_srcrect, dstrect, origin, 0.0f, RAYWHITE);
      }

      if(debug_palette) {
        if(current_format == RTGFMT_8BIT_CLUT) {
          Rectangle clut_srcrect = {0, 0, 256, 1};
          Rectangle clut_dstrect = {0, 0, 1024, 8};
          DrawTexturePro(raylib_clut_texture, clut_srcrect, clut_dstrect, origin, 0.0f, RAYWHITE);
        }
      }

      if(show_fps) {
        DrawFPS(pi_screen_width - 128, 0);
      }

      EndDrawing();

      rtg_output_in_vblank = 1;
      cur_rtg_frame++;
      size_t frame_addr_offset = (size_t)current_addr;
      size_t frame_needed = (size_t)current_pitch * height;

      if(current_pitch < row_bytes) {
        LOG_WARN("[RTG/RAYLIB] Frame pitch too small: pitch=%u row_bytes=%zu\n", current_pitch,
                 row_bytes);
      } else if(frame_addr_offset >= rtg_mem_size || frame_needed > rtg_mem_size - frame_addr_offset) {
        LOG_WARN("[RTG/RAYLIB] Framebuffer OOB: addr=0x%08X needed=%zu limit=%zu\n", current_addr,
                 needed, rtg_mem_size);
      } else if(current_format == RTGFMT_RGB565_BE || current_format == RTGFMT_RGB555_BE) {
        if((current_pitch % 2) != 0) {
          LOG_WARN("[RTG/RAYLIB] 16-bit pitch not aligned: pitch=%u\n", current_pitch);
        } else {
          if(indexed_buf_size < tight_size) {
            void* resized = realloc(indexed_buf, tight_size);
            if(!resized) {
              LOG_ERROR("[RTG/RAYLIB] Failed to allocate indexed buffer (%zu bytes)\n", tight_size);
            } else {
              indexed_buf = resized;
              indexed_buf_size = tight_size;
            }
          }
          if(indexed_buf) {
            size_t src_stride = current_pitch / 2;
            for (uint16_t y = 0; y < height; y++) {
              for (uint16_t x = 0; x < width; x++) {
                size_t src_idx = (x + (y * src_stride)) * sizeof(uint16_t);
                const uint8_t* src_ptr = data->memory + addr + src_idx;
                indexed_buf[x + (y * width)] = load_u16_be(src_ptr);
              }
            }
            UpdateTexture(raylib_texture, indexed_buf);
          }
        }
      } else if(current_pitch != row_bytes) {
        if(tight_buf_size < tight_size) {
          void* resized = realloc(tight_buf, tight_size);
          if(!resized) {
            LOG_ERROR("[RTG/RAYLIB] Failed to allocate tight buffer (%zu bytes)\n", tight_size);
          } else {
            tight_buf = resized;
            tight_buf_size = tight_size;
          }
        }
        if(tight_buf) {
          rtg_copy_tight_rows(tight_buf, data->memory + addr, row_bytes, current_pitch, height);
          UpdateTexture(raylib_texture, tight_buf);
        }
      } else {
        UpdateTexture(raylib_texture, data->memory + addr);
      }
      if(cursor_image_updated) {
        if(clut_cursor_enabled) {
          UpdateTexture(raylib_cursor_texture, clut_cursor_texture_data);
        } else {
          UpdateTexture(raylib_cursor_texture, cursor_data);
        }
        cursor_image_updated = 0;
      }
      if(palette_updated) {
        UpdateTexture(raylib_clut_texture, palette);
        palette_updated = 0;
      }
      if(frame_no < 3) {
        LOG_DEBUG("[RTG/RAYLIB] Frame %u end\n", frame_no);
      }
      frame_no++;
      updating_screen = 0;
    } else {
      BeginDrawing();
      ClearBackground(bef);
      // DrawText("RTG is currently sleeping.", 16, 16, 12, RAYWHITE);
      EndDrawing();
    }
    if(pitch != *data->pitch || height != *data->height || width != *data->width ||
        format != *data->format) {
      LOG_INFO("[RTG/RAYLIB] Mode change detected after frame; reinitializing.\n");
      reinit = 1;
      goto shutdown_raylib;
    }
    if(emulator_exiting) {
      goto shutdown_raylib;
    }
    if(shutdown) {
      break;
    }
  }

  shutdown = 0;
  rtg_initialized = 0;
  LOG_INFO("RTG thread shut down.\n");

shutdown_raylib:;
  // shutdown raylib
  if(reinit) {
    goto reinit_raylib;
  }

  if(indexed_buf) {
    free(indexed_buf);
  }
  if(tight_buf) {
    free(tight_buf);
  }

  UnloadTexture(raylib_texture);
  UnloadShader(clut_shader);

  CloseWindow();

  return args;
}

void rtg_set_screen_width(uint32_t width) {
  if(width == 0) {
    pi_screen_width_set = 0;
    return;
  }
  pi_screen_width = (int32_t)width;
  pi_screen_width_set = 1;
}

void rtg_set_screen_height(uint32_t height) {
  if(height == 0) {
    pi_screen_height_set = 0;
    return;
  }
  pi_screen_height = (int32_t)height;
  pi_screen_height_set = 1;
}

void rtg_set_clut_entry(uint8_t index, uint32_t xrgb) {
  // palette[index] = xrgb;
  unsigned char* src = (unsigned char*)&xrgb;
  unsigned char* dst = (unsigned char*)&palette[index];
  palette_updated = 1;
  dst[0] = src[2];
  dst[1] = src[1];
  dst[2] = src[0];
  dst[3] = 0xFF;
}

void rtg_init_display(void) {
  int err;
  rtg_on = 1;

  if(!rtg_initialized) {
#if USE_VC
    if(rtg_dpms) {
      vc_tv_hdmi_power_on_preferred();
    }
#endif

    err = pthread_create(&thread_id, NULL, &rtgThread, (void*)&rtg_share_data);
    if(err != 0) {
      rtg_on = 0;
      display_enabled = 0xFF;
      LOG_ERROR("can't create RTG thread :[%s]", strerror(err));
    } else {
      rtg_initialized = 1;
      pthread_setname_np(thread_id, "pistorm: rtg");
      LOG_INFO("RTG Thread created successfully\n");
    }
  }
  LOG_INFO("RTG display enabled.\n");
}

void rtg_shutdown_display(void) {
  LOG_INFO("RTG display disabled.\n");

  rtg_on = 0;

  if(!emulator_exiting) {
    display_enabled = 0xFF;
    return;
  }

  shutdown = 1;

#if USE_VC
  if(rtg_dpms) {
    vc_tv_power_off();
  }
#endif

  pthread_join(thread_id, NULL);

  display_enabled = 0xFF;
}

void rtg_show_clut_cursor(uint8_t show) {
  if(clut_cursor_enabled != show) {
    while (rtg_on && !updating_screen)
      usleep(0);
    cursor_image_updated = 1;
  }
  clut_cursor_enabled = show;
}

void rtg_set_clut_cursor(uint8_t* bmp, uint32_t* pal, int16_t offs_x, int16_t offs_y, uint16_t w,
                         uint16_t h, uint8_t mask_color) {
  if(bmp == NULL) {
    memset(clut_cursor_texture_data, 0x00, (256 * 256) * sizeof(uint32_t));
    cursor_image_updated = 1;
  }
  if(bmp != NULL && pal != NULL) {
    memset(clut_cursor_texture_data, 0x00, (256 * 256) * sizeof(uint32_t));
    mouse_cursor_w = w;
    mouse_cursor_h = h;
    mouse_cursor_x_adj = -offs_x;
    mouse_cursor_y_adj = -offs_y;
    for (uint8_t y = 0; y < mouse_cursor_h; y++) {
      for (uint8_t x = 0; x < mouse_cursor_w; x++) {
        uint8_t clut_index = bmp[x + (y * w)];
        if(bmp[x + (y * w)] != mask_color) {
          uint32_t col;
          memcpy(&col, (uint8_t*)pal + (clut_index * sizeof(uint32_t)), sizeof(col));
          clut_cursor_texture_data[x + (y * 256)] = (be32toh(col) | 0xFF000000);
        }
      }
    }
    while (rtg_on && !updating_screen) usleep(0);

    cursor_image_updated = 1;
  }
}

void rtg_enable_mouse_cursor(uint8_t enable) {
  mouse_cursor_enabled = enable;
}

void rtg_set_mouse_cursor_pos(int16_t x, int16_t y) {
  mouse_cursor_x = x;
  mouse_cursor_y = y;
}

static uint8_t clut_cursor_data[256 * 256];

void update_mouse_cursor(uint8_t* src) {
  if(src != NULL) {
    memset(clut_cursor_data, 0x00, 256 * 256);
    uint8_t cur_bit = 0x80;
    uint8_t line_pitch = (uint8_t)((mouse_cursor_w / 8) * 2);

    for (uint16_t y = 0; y < mouse_cursor_h; y++) {
      for (uint16_t x = 0; x < mouse_cursor_w; x++) {
        if(src[(x / 8) + (line_pitch * y)] & cur_bit)
          clut_cursor_data[x + (y * 256)] |= 0x01;
        if(src[(x / 8) + (line_pitch * y) + (mouse_cursor_w / 8)] & cur_bit)
          clut_cursor_data[x + (y * 256)] |= 0x02;
        cur_bit >>= 1;
        if(cur_bit == 0x00)
          cur_bit = 0x80;
      }
      cur_bit = 0x80;
    }
  }

  for (uint16_t y = 0; y < mouse_cursor_h; y++) {
    for (uint16_t x = 0; x < mouse_cursor_w; x++) {
      cursor_data[x + (y * 256)] = cursor_palette[clut_cursor_data[x + (y * 256)]];
    }
  }

  while (rtg_on && !updating_screen) usleep(0);

  cursor_image_updated = 1;
}

void rtg_set_cursor_clut_entry(uint8_t r, uint8_t g, uint8_t b, uint8_t idx) {
  uint32_t color = 0;
  unsigned char* dst = (unsigned char*)&color;

  dst[0] = r;
  dst[1] = g;
  dst[2] = b;
  dst[3] = 0xFF;
  if(cursor_palette[idx + 1] != color) {
    cursor_palette[0] = 0;
    cursor_palette[idx + 1] = color;
    update_mouse_cursor(NULL);
  }
}

static uint8_t old_mouse_w, old_mouse_h;
static uint8_t old_mouse_data[256];

void rtg_set_mouse_cursor_image(uint8_t* src, uint8_t w, uint8_t h) {
  uint8_t new_cursor_data = 0;

  mouse_cursor_w = w;
  mouse_cursor_h = h;

  if(memcmp(src, old_mouse_data, (w / 8 * h)) != 0) {
    new_cursor_data = 1;
  }

  if(old_mouse_w != w || old_mouse_h != h || new_cursor_data) {
    old_mouse_w = w;
    old_mouse_h = h;
    update_mouse_cursor(src);
  }
}

void rtg_show_fps(uint8_t enable) {
  show_fps = (enable != 0);
}

void rtg_palette_debug(uint8_t enable) {
  debug_palette = (enable != 0);
}

void rtg_set_scale_mode(uint16_t _scale_mode) {
  switch (_scale_mode) {
  case PIGFX_SCALE_INTEGER_MAX:
  case PIGFX_SCALE_FULL_ASPECT:
  case PIGFX_SCALE_FULL_43:
  case PIGFX_SCALE_FULL_169:
  case PIGFX_SCALE_FULL:
  case PIGFX_SCALE_NONE:
    scale_mode = (uint8_t)_scale_mode;
    rtg_scale_output(rtg_display_width, rtg_display_height);
    break;
  case PIGFX_SCALE_CUSTOM:
  case PIGFX_SCALE_CUSTOM_RECT:
    LOG_WARN("[!!!RTG] Tried to set RTG scale mode to custom or custom rect using the wrong "
             "function. Ignored.\n");
    break;
  }
}

uint16_t rtg_get_scale_mode(void) {
  return scale_mode;
}

void rtg_set_scale_rect(uint16_t _scale_mode, int16_t x1, int16_t y1, int16_t x2, int16_t y2) {
  scale_mode = (uint8_t)_scale_mode;

  origin.x = 0.0f;
  origin.y = 0.0f;
  mouse_cursor_x_adj = x1;
  mouse_cursor_y_adj = y1;
  dstscale.x = (float)x1;
  dstscale.y = (float)y1;

  switch (scale_mode) {
  case PIGFX_SCALE_CUSTOM_RECT:
    dstscale.width = (float)x2;
    dstscale.height = (float)y2;
    break;
  case PIGFX_SCALE_CUSTOM:
    dstscale.width = (float)x2 - (float)x1;
    dstscale.height = (float)y2 - (float)y1;
    break;
  }

  rtg_scale_output(rtg_display_width, rtg_display_height);
}

void rtg_set_scale_filter(uint16_t _filter_mode) {
  filter_mode = (uint8_t)_filter_mode;
}

uint16_t rtg_get_scale_filter(void) {
  return filter_mode;
}
