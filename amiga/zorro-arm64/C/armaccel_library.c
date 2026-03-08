// SPDX-License-Identifier: MIT
#include <exec/io.h>
#include <exec/ports.h>
#include <exec/types.h>

#include <dos/dos.h>
#include <dos/dosextens.h>
#include <intuition/intuition.h>
#include <utility/tagitem.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arm64_accel_regs.h"
#include "armaccel_iocmds.h"
#include "armaccel_library.h"

#define ELF_EHDR_SIZE 64u
#define ELF64_PHDR_SIZE 56u

#define ELFCLASS64 2u
#define ELFDATA2LSB 1u
#define EV_CURRENT 1u
#define ET_EXEC 2u
#define ET_DYN 3u
#define EM_AARCH64 183u
#define PT_NOTE 4u

#define ARMACCEL_AVAILABLE_SERVICES                                              \
  (ARMACCEL_CAP_WINDOW | ARMACCEL_CAP_REQUESTER | ARMACCEL_CAP_SURFACE |       \
   ARMACCEL_CAP_INPUT | ARMACCEL_CAP_FILE | ARMACCEL_CAP_TIMER)
#define ARMACCEL_DEVICE_NAME "armaccel.device"

#define ARMACCEL_SVC_MAX_SESSIONS 4u
#define ARMACCEL_SVC_MAX_WINDOWS 8u
#define ARMACCEL_SVC_MAX_SURFACES 8u
#define ARMACCEL_SVC_MAX_FILES 8u
#define ARMACCEL_SVC_DEFAULT_EVENT_SIZE (sizeof(struct armaccel_event_v1))
#define ARMACCEL_SVC_DEFAULT_TICK_HZ 50u
#define ARMACCEL_SVC_FILE_OPENFLAG_WRITE 0x00000001u

struct armaccel_service_session {
  ULONG in_use;
  ULONG handle;
  ULONG event_ring_desc_offset;
  ULONG event_data_offset;
  ULONG event_ring_capacity;
  ULONG event_ring_event_size;
  ULONG event_ring_head;
  ULONG event_ring_tail;
  ULONG event_ring_flags;
  ULONG event_ring_overflow_count;
  ULONG event_seq;
};

struct armaccel_service_window {
  ULONG in_use;
  ULONG handle;
  ULONG session_handle;
  struct Window *intuition_window;
};

struct armaccel_service_surface {
  ULONG in_use;
  ULONG handle;
  ULONG session_handle;
  ULONG window_handle;
  ULONG pixel_format;
  ULONG width;
  ULONG height;
  ULONG stride;
  ULONG buffer_offset;
  ULONG buffer_size;
};

struct armaccel_service_file {
  ULONG in_use;
  ULONG handle;
  ULONG session_handle;
  BPTR file_handle;
};

struct armaccel_service_runtime {
  ULONG next_handle;
  struct armaccel_service_session sessions[ARMACCEL_SVC_MAX_SESSIONS];
  struct armaccel_service_window windows[ARMACCEL_SVC_MAX_WINDOWS];
  struct armaccel_service_surface surfaces[ARMACCEL_SVC_MAX_SURFACES];
  struct armaccel_service_file files[ARMACCEL_SVC_MAX_FILES];
};

struct elf_header_info {
  ULONG file_size;
  unsigned long long phoff;
  unsigned short phentsize;
  unsigned short phnum;
};

static ULONG svc_be32_read(volatile UBYTE *base, ULONG off) {
  return ((ULONG)base[off + 0u] << 24u) | ((ULONG)base[off + 1u] << 16u) |
         ((ULONG)base[off + 2u] << 8u) | (ULONG)base[off + 3u];
}

static void svc_be32_write(volatile UBYTE *base, ULONG off, ULONG value) {
  base[off + 0u] = (UBYTE)((value >> 24u) & 0xFFu);
  base[off + 1u] = (UBYTE)((value >> 16u) & 0xFFu);
  base[off + 2u] = (UBYTE)((value >> 8u) & 0xFFu);
  base[off + 3u] = (UBYTE)(value & 0xFFu);
}

static int svc_bounds_ok(ULONG offset, ULONG size) {
  if (offset >= ARM64_ACCEL_Z2_SIZE) {
    return 0;
  }
  if (size > (ARM64_ACCEL_Z2_SIZE - offset)) {
    return 0;
  }
  return 1;
}

static char *svc_copy_string(volatile UBYTE *base, ULONG off, ULONG size) {
  char *str;
  ULONG i;

  if (size == 0u) {
    return NULL;
  }
  if (!svc_bounds_ok(off, size)) {
    return NULL;
  }

  str = (char *)malloc((size_t)size + 1u);
  if (str == NULL) {
    return NULL;
  }
  for (i = 0u; i < size; i++) {
    str[i] = (char)base[off + i];
  }
  str[size] = '\0';
  return str;
}

static ULONG svc_next_handle(struct armaccel_service_runtime *rt) {
  ULONG h = rt->next_handle + 1u;
  if (h == ARMACCEL_HANDLE_INVALID) {
    h = 1u;
  }
  rt->next_handle = h;
  return h;
}

static struct armaccel_service_session *svc_find_session(struct armaccel_service_runtime *rt,
                                                         ULONG handle) {
  ULONG i;
  if (handle == ARMACCEL_HANDLE_INVALID) {
    return NULL;
  }
  for (i = 0u; i < ARMACCEL_SVC_MAX_SESSIONS; i++) {
    if ((rt->sessions[i].in_use != 0u) && (rt->sessions[i].handle == handle)) {
      return &rt->sessions[i];
    }
  }
  return NULL;
}

static struct armaccel_service_window *svc_find_window(struct armaccel_service_runtime *rt,
                                                       ULONG handle) {
  ULONG i;
  if (handle == ARMACCEL_HANDLE_INVALID) {
    return NULL;
  }
  for (i = 0u; i < ARMACCEL_SVC_MAX_WINDOWS; i++) {
    if ((rt->windows[i].in_use != 0u) && (rt->windows[i].handle == handle)) {
      return &rt->windows[i];
    }
  }
  return NULL;
}

static struct armaccel_service_surface *svc_find_surface(struct armaccel_service_runtime *rt,
                                                         ULONG handle) {
  ULONG i;
  if (handle == ARMACCEL_HANDLE_INVALID) {
    return NULL;
  }
  for (i = 0u; i < ARMACCEL_SVC_MAX_SURFACES; i++) {
    if ((rt->surfaces[i].in_use != 0u) && (rt->surfaces[i].handle == handle)) {
      return &rt->surfaces[i];
    }
  }
  return NULL;
}

static struct armaccel_service_file *svc_find_file(struct armaccel_service_runtime *rt, ULONG handle) {
  ULONG i;
  if (handle == ARMACCEL_HANDLE_INVALID) {
    return NULL;
  }
  for (i = 0u; i < ARMACCEL_SVC_MAX_FILES; i++) {
    if ((rt->files[i].in_use != 0u) && (rt->files[i].handle == handle)) {
      return &rt->files[i];
    }
  }
  return NULL;
}

static void svc_frame_finish(volatile UBYTE *base, ULONG result0, ULONG result1, ULONG is_error) {
  ULONG frame_off = ARM64_ACCEL_JOBDESC_OFFSET + ARMACCEL_SVC_FRAME_JOBDESC_OFF;
  svc_be32_write(base, frame_off + ARMACCEL_SVC_OFF_RESULT0, result0);
  svc_be32_write(base, frame_off + ARMACCEL_SVC_OFF_RESULT1, result1);
  svc_be32_write(base, frame_off + ARMACCEL_SVC_OFF_STATE,
                 (is_error != 0u) ? ARMACCEL_SVC_STATE_ERROR : ARMACCEL_SVC_STATE_DONE);
}

static int svc_push_event(volatile UBYTE *base, struct armaccel_service_session *session, ULONG event_class,
                          ULONG event_code, ULONG object_handle, ULONG data0, ULONG data1, ULONG data2,
                          ULONG data3) {
  ULONG event_size;
  ULONG head;
  ULONG tail;
  ULONG next_head;
  ULONG event_off;
  ULONG tick;
  struct DateStamp ds;

  if ((session == NULL) || (session->event_ring_capacity == 0u) ||
      (session->event_ring_event_size < ARMACCEL_SVC_DEFAULT_EVENT_SIZE)) {
    return 1;
  }
  event_size = session->event_ring_event_size;
  head = session->event_ring_head;
  tail = session->event_ring_tail;
  next_head = (head + 1u) % session->event_ring_capacity;
  if (next_head == tail) {
    session->event_ring_overflow_count++;
    svc_be32_write(base,
                   session->event_ring_desc_offset +
                       offsetof(struct armaccel_event_ring_v1, overflow_count),
                   session->event_ring_overflow_count);
    return 2;
  }

  event_off = session->event_data_offset + (head * event_size);
  if (!svc_bounds_ok(event_off, event_size)) {
    return 3;
  }

  DateStamp(&ds);
  tick = (ULONG)ds.ds_Tick + ((ULONG)ds.ds_Minute * 3000u);
  tick += ((ULONG)ds.ds_Days * 24u * 60u * 3000u);
  svc_be32_write(base, event_off + offsetof(struct armaccel_event_v1, seq), session->event_seq++);
  svc_be32_write(base, event_off + offsetof(struct armaccel_event_v1, session_handle), session->handle);
  svc_be32_write(base, event_off + offsetof(struct armaccel_event_v1, event_class), event_class);
  svc_be32_write(base, event_off + offsetof(struct armaccel_event_v1, event_code), event_code);
  svc_be32_write(base, event_off + offsetof(struct armaccel_event_v1, object_handle), object_handle);
  svc_be32_write(base, event_off + offsetof(struct armaccel_event_v1, flags), 0u);
  svc_be32_write(base, event_off + offsetof(struct armaccel_event_v1, data0), data0);
  svc_be32_write(base, event_off + offsetof(struct armaccel_event_v1, data1), data1);
  svc_be32_write(base, event_off + offsetof(struct armaccel_event_v1, data2), data2);
  svc_be32_write(base, event_off + offsetof(struct armaccel_event_v1, data3), data3);
  svc_be32_write(base, event_off + offsetof(struct armaccel_event_v1, tick_hi), 0u);
  svc_be32_write(base, event_off + offsetof(struct armaccel_event_v1, tick_lo), tick);

  session->event_ring_head = next_head;
  svc_be32_write(base, session->event_ring_desc_offset + offsetof(struct armaccel_event_ring_v1, head),
                 session->event_ring_head);
  svc_be32_write(base, session->event_ring_desc_offset + offsetof(struct armaccel_event_ring_v1, tail),
                 session->event_ring_tail);
  return 0;
}

static void svc_write_caps(volatile UBYTE *base, ULONG caps_off) {
  if (!svc_bounds_ok(caps_off, (ULONG)sizeof(struct armaccel_caps_v1))) {
    return;
  }

  svc_be32_write(base, caps_off + offsetof(struct armaccel_caps_v1, abi_major), ARMACCEL_SVC_ABI_MAJOR);
  svc_be32_write(base, caps_off + offsetof(struct armaccel_caps_v1, abi_minor), ARMACCEL_SVC_ABI_MINOR);
  svc_be32_write(base, caps_off + offsetof(struct armaccel_caps_v1, service_caps),
                 ARMACCEL_AVAILABLE_SERVICES);
  svc_be32_write(base, caps_off + offsetof(struct armaccel_caps_v1, profile_caps),
                 ARMACCEL_PROFILE_CORE | ARMACCEL_PROFILE_APP);
  svc_be32_write(base, caps_off + offsetof(struct armaccel_caps_v1, feature_flags),
                 ARMACCEL_FEAT_EVENT_RING | ARMACCEL_FEAT_EVENT_TIMESTAMP);
  svc_be32_write(base, caps_off + offsetof(struct armaccel_caps_v1, coord_format),
                 ARMACCEL_COORD_FORMAT_S32);
  svc_be32_write(base, caps_off + offsetof(struct armaccel_caps_v1, size_format),
                 ARMACCEL_SIZE_FORMAT_U32);
  svc_be32_write(base, caps_off + offsetof(struct armaccel_caps_v1, time_unit_sleep),
                 ARMACCEL_TIME_UNIT_MS);
  svc_be32_write(base, caps_off + offsetof(struct armaccel_caps_v1, time_unit_event),
                 ARMACCEL_TIME_UNIT_TICKS);
  svc_be32_write(base, caps_off + offsetof(struct armaccel_caps_v1, pixel_format_caps),
                 (1u << ARMACCEL_PIXFMT_CLUT8));
  svc_be32_write(base, caps_off + offsetof(struct armaccel_caps_v1, max_inflight_per_session),
                 ARMACCEL_SVC_MAX_INFLIGHT_PER_SESSION);
  svc_be32_write(base, caps_off + offsetof(struct armaccel_caps_v1, max_windows),
                 ARMACCEL_SVC_MAX_WINDOWS);
  svc_be32_write(base, caps_off + offsetof(struct armaccel_caps_v1, max_surfaces),
                 ARMACCEL_SVC_MAX_SURFACES);
  svc_be32_write(base, caps_off + offsetof(struct armaccel_caps_v1, max_files), ARMACCEL_SVC_MAX_FILES);
  svc_be32_write(base, caps_off + offsetof(struct armaccel_caps_v1, max_blob_bytes),
                 ARM64_ACCEL_SHARED_SIZE);
  svc_be32_write(base, caps_off + offsetof(struct armaccel_caps_v1, max_event_ring_events), 1024u);
  svc_be32_write(base, caps_off + offsetof(struct armaccel_caps_v1, tick_hz_hi), 0u);
  svc_be32_write(base, caps_off + offsetof(struct armaccel_caps_v1, tick_hz_lo),
                 ARMACCEL_SVC_DEFAULT_TICK_HZ);
  svc_be32_write(base, caps_off + offsetof(struct armaccel_caps_v1, reserved0), 0u);
  svc_be32_write(base, caps_off + offsetof(struct armaccel_caps_v1, reserved1), 0u);
}

static void svc_runtime_init(struct armaccel_service_runtime *rt) {
  memset(rt, 0, sizeof(*rt));
  rt->next_handle = 1u;
}

static void svc_runtime_cleanup(struct armaccel_service_runtime *rt) {
  ULONG i;
  for (i = 0u; i < ARMACCEL_SVC_MAX_WINDOWS; i++) {
    if ((rt->windows[i].in_use != 0u) && (rt->windows[i].intuition_window != NULL)) {
      CloseWindow(rt->windows[i].intuition_window);
      rt->windows[i].intuition_window = NULL;
    }
    rt->windows[i].in_use = 0u;
  }
  for (i = 0u; i < ARMACCEL_SVC_MAX_FILES; i++) {
    if ((rt->files[i].in_use != 0u) && (rt->files[i].file_handle != (BPTR)0)) {
      Close(rt->files[i].file_handle);
    }
    rt->files[i].in_use = 0u;
    rt->files[i].file_handle = (BPTR)0;
  }
  for (i = 0u; i < ARMACCEL_SVC_MAX_SURFACES; i++) {
    rt->surfaces[i].in_use = 0u;
  }
  for (i = 0u; i < ARMACCEL_SVC_MAX_SESSIONS; i++) {
    rt->sessions[i].in_use = 0u;
  }
}

static unsigned short read_u16_le(const unsigned char *p) {
  return (unsigned short)((unsigned short)p[0u] | ((unsigned short)p[1u] << 8));
}

static unsigned long read_u32_le(const unsigned char *p) {
  return (unsigned long)((unsigned long)p[0u] | ((unsigned long)p[1u] << 8) |
                         ((unsigned long)p[2u] << 16) | ((unsigned long)p[3u] << 24));
}

static unsigned long long read_u64_le(const unsigned char *p) {
  unsigned long long lo = (unsigned long long)read_u32_le(p);
  unsigned long long hi = (unsigned long long)read_u32_le(p + 4u);
  return lo | (hi << 32);
}

static ULONG align_up(ULONG value, ULONG alignment) {
  if ((alignment == 0u) || ((alignment & (alignment - 1u)) != 0u)) {
    alignment = 4u;
  }
  return (value + alignment - 1u) & ~(alignment - 1u);
}

static int load_file_bytes(const char *path, unsigned char **out_buf, ULONG *out_size) {
  FILE *fp;
  unsigned char *buf;
  long size_long;

  if ((path == NULL) || (out_buf == NULL) || (out_size == NULL)) {
    return 1;
  }
  *out_buf = NULL;
  *out_size = 0u;

  fp = fopen(path, "rb");
  if (fp == NULL) {
    return 2;
  }
  if (fseek(fp, 0L, SEEK_END) != 0) {
    fclose(fp);
    return 3;
  }
  size_long = ftell(fp);
  if (size_long <= 0L) {
    fclose(fp);
    return 4;
  }
  if (fseek(fp, 0L, SEEK_SET) != 0) {
    fclose(fp);
    return 5;
  }

  buf = (unsigned char *)malloc((size_t)size_long);
  if (buf == NULL) {
    fclose(fp);
    return 6;
  }

  if (fread(buf, 1u, (size_t)size_long, fp) != (size_t)size_long) {
    free(buf);
    fclose(fp);
    return 7;
  }
  fclose(fp);

  *out_buf = buf;
  *out_size = (ULONG)size_long;
  return 0;
}

static int validate_elf64_aarch64(const unsigned char *buf, ULONG size,
                                  struct elf_header_info *out_info) {
  unsigned short e_type;
  unsigned short e_machine;
  unsigned short e_ehsize;
  unsigned short e_phnum;
  unsigned short e_phentsize;
  unsigned long e_version;
  unsigned long long e_phoff;

  if ((buf == NULL) || (out_info == NULL) || (size < ELF_EHDR_SIZE)) {
    return 1;
  }

  if ((buf[0u] != 0x7Fu) || (buf[1u] != 'E') || (buf[2u] != 'L') || (buf[3u] != 'F')) {
    return 2;
  }
  if (buf[4u] != ELFCLASS64) {
    return 3;
  }
  if (buf[5u] != ELFDATA2LSB) {
    return 4;
  }
  if (buf[6u] != EV_CURRENT) {
    return 5;
  }

  e_type = read_u16_le(buf + 16u);
  e_machine = read_u16_le(buf + 18u);
  e_version = read_u32_le(buf + 20u);
  e_phoff = read_u64_le(buf + 32u);
  e_ehsize = read_u16_le(buf + 52u);
  e_phentsize = read_u16_le(buf + 54u);
  e_phnum = read_u16_le(buf + 56u);

  if ((e_type != ET_EXEC) && (e_type != ET_DYN)) {
    return 6;
  }
  if (e_machine != EM_AARCH64) {
    return 7;
  }
  if (e_version != EV_CURRENT) {
    return 8;
  }
  if ((e_ehsize < ELF_EHDR_SIZE) || (e_phoff == 0ull) || (e_phnum == 0u)) {
    return 9;
  }
  if (e_phentsize < ELF64_PHDR_SIZE) {
    return 10;
  }

  out_info->file_size = size;
  out_info->phoff = e_phoff;
  out_info->phentsize = e_phentsize;
  out_info->phnum = e_phnum;
  return 0;
}

static int parse_armaccel_note(const unsigned char *buf, const struct elf_header_info *eh,
                               struct ArmAccelELFInfo *out_info) {
  unsigned short i;

  if ((buf == NULL) || (eh == NULL) || (out_info == NULL)) {
    return -1;
  }

  for (i = 0u; i < eh->phnum; i++) {
    unsigned long long phdr_off_ull = eh->phoff + ((unsigned long long)i * eh->phentsize);
    ULONG phdr_off;
    unsigned long p_type;
    unsigned long long p_offset_ull;
    unsigned long long p_filesz_ull;
    unsigned long long p_align_ull;
    ULONG note_align;
    ULONG cursor;
    ULONG end;

    if (phdr_off_ull > (unsigned long long)(eh->file_size - ELF64_PHDR_SIZE)) {
      return -1;
    }

    phdr_off = (ULONG)phdr_off_ull;
    p_type = read_u32_le(buf + phdr_off + 0u);
    if (p_type != PT_NOTE) {
      continue;
    }

    p_offset_ull = read_u64_le(buf + phdr_off + 8u);
    p_filesz_ull = read_u64_le(buf + phdr_off + 32u);
    p_align_ull = read_u64_le(buf + phdr_off + 48u);
    note_align = (ULONG)p_align_ull;
    if (note_align == 0u) {
      note_align = 4u;
    }

    if ((p_offset_ull > eh->file_size) || (p_filesz_ull > (eh->file_size - p_offset_ull))) {
      return -1;
    }

    cursor = (ULONG)p_offset_ull;
    end = cursor + (ULONG)p_filesz_ull;

    while (cursor + 12u <= end) {
      ULONG namesz = (ULONG)read_u32_le(buf + cursor + 0u);
      ULONG descsz = (ULONG)read_u32_le(buf + cursor + 4u);
      ULONG note_type = (ULONG)read_u32_le(buf + cursor + 8u);
      ULONG namesz_padded;
      ULONG descsz_padded;
      ULONG name_off;
      ULONG desc_off;

      cursor += 12u;
      namesz_padded = align_up(namesz, 4u);
      descsz_padded = align_up(descsz, note_align);

      if ((namesz_padded > (end - cursor)) ||
          (descsz_padded > (end - cursor - namesz_padded))) {
        break;
      }

      name_off = cursor;
      desc_off = cursor + namesz_padded;

      if ((note_type == ARMACCEL_NOTE_TYPE) && (namesz >= 8u) &&
          (memcmp(buf + name_off, ARMACCEL_NOTE_NAME, 8u) == 0) && (descsz >= 20u)) {
        ULONG note_magic = (ULONG)read_u32_le(buf + desc_off + 0u);
        ULONG note_version = (ULONG)read_u16_le(buf + desc_off + 4u);
        ULONG note_abi_major = (ULONG)read_u16_le(buf + desc_off + 6u);
        ULONG note_personality = (ULONG)read_u32_le(buf + desc_off + 8u);
        ULONG note_required_services = (ULONG)read_u32_le(buf + desc_off + 12u);
        ULONG note_app_class = (ULONG)read_u32_le(buf + desc_off + 16u);

        if ((note_magic != ARMACCEL_NOTE_DESC_MAGIC) ||
            (note_version != ARMACCEL_NOTE_DESC_VERSION) ||
            (note_personality != ARMACCEL_PERSONALITY_ARM64)) {
          return -1;
        }

        out_info->has_personality_tag = 1u;
        out_info->abi_major = note_abi_major;
        out_info->required_services = note_required_services;
        out_info->app_class = note_app_class;
        return 0;
      }

      cursor = desc_off + descsz_padded;
    }
  }

  return 1;
}

static int open_armaccel_device(struct MsgPort **out_port, struct ArmAccelIORequest **out_req) {
  struct MsgPort *port;
  struct ArmAccelIORequest *req;
  BYTE open_err;

  if ((out_port == NULL) || (out_req == NULL)) {
    return 1;
  }

  *out_port = NULL;
  *out_req = NULL;

  port = CreateMsgPort();
  if (port == NULL) {
    return 2;
  }

  req = (struct ArmAccelIORequest *)CreateIORequest(port, sizeof(*req));
  if (req == NULL) {
    DeleteMsgPort(port);
    return 3;
  }

  memset(req, 0, sizeof(*req));
  open_err = OpenDevice((STRPTR)ARMACCEL_DEVICE_NAME, 0u, (struct IORequest *)req, 0u);
  if (open_err != 0) {
    DeleteIORequest((struct IORequest *)req);
    DeleteMsgPort(port);
    return 4;
  }

  *out_port = port;
  *out_req = req;
  return 0;
}

static void close_armaccel_device(struct MsgPort *port, struct ArmAccelIORequest *req) {
  if (req != NULL) {
    CloseDevice((struct IORequest *)req);
    DeleteIORequest((struct IORequest *)req);
  }
  if (port != NULL) {
    DeleteMsgPort(port);
  }
}

static void svc_close_session_children(struct armaccel_service_runtime *rt, ULONG session_handle) {
  ULONG i;
  for (i = 0u; i < ARMACCEL_SVC_MAX_WINDOWS; i++) {
    if ((rt->windows[i].in_use != 0u) && (rt->windows[i].session_handle == session_handle)) {
      if (rt->windows[i].intuition_window != NULL) {
        CloseWindow(rt->windows[i].intuition_window);
      }
      memset(&rt->windows[i], 0, sizeof(rt->windows[i]));
    }
  }
  for (i = 0u; i < ARMACCEL_SVC_MAX_SURFACES; i++) {
    if ((rt->surfaces[i].in_use != 0u) && (rt->surfaces[i].session_handle == session_handle)) {
      memset(&rt->surfaces[i], 0, sizeof(rt->surfaces[i]));
    }
  }
  for (i = 0u; i < ARMACCEL_SVC_MAX_FILES; i++) {
    if ((rt->files[i].in_use != 0u) && (rt->files[i].session_handle == session_handle)) {
      if (rt->files[i].file_handle != (BPTR)0) {
        Close(rt->files[i].file_handle);
      }
      memset(&rt->files[i], 0, sizeof(rt->files[i]));
    }
  }
}

static void svc_handle_session(volatile UBYTE *base, struct armaccel_service_runtime *rt, ULONG opcode,
                               ULONG arg0, ULONG arg1, ULONG *out_result0, ULONG *out_result1) {
  ULONG i;
  (void)arg1;

  *out_result0 = ARMACCEL_SVC_RES_UNSUPPORTED;
  *out_result1 = 0u;

  if (opcode == ARMACCEL_SVC_SESSION_OPEN) {
    ULONG req_off = arg0;
    struct armaccel_service_session *session = NULL;
    ULONG caps_off;
    if (!svc_bounds_ok(req_off, (ULONG)sizeof(struct armaccel_session_open_v1))) {
      *out_result0 = ARMACCEL_SVC_RES_INVALID_ARG;
      return;
    }
    for (i = 0u; i < ARMACCEL_SVC_MAX_SESSIONS; i++) {
      if (rt->sessions[i].in_use == 0u) {
        session = &rt->sessions[i];
        break;
      }
    }
    if (session == NULL) {
      *out_result0 = ARMACCEL_SVC_RES_BUSY;
      return;
    }

    memset(session, 0, sizeof(*session));
    session->in_use = 1u;
    session->handle = svc_next_handle(rt);

    {
      ULONG ring_desc_off =
          svc_be32_read(base, req_off + offsetof(struct armaccel_session_open_v1, event_ring_desc_offset));
      ULONG ring_desc_size =
          svc_be32_read(base, req_off + offsetof(struct armaccel_session_open_v1, event_ring_desc_size));

      if ((ring_desc_off != 0u) && (ring_desc_size >= (ULONG)sizeof(struct armaccel_event_ring_v1)) &&
          svc_bounds_ok(ring_desc_off, ring_desc_size)) {
        session->event_ring_desc_offset = ring_desc_off;
        session->event_data_offset =
            svc_be32_read(base, ring_desc_off + offsetof(struct armaccel_event_ring_v1, offset));
        session->event_ring_capacity =
            svc_be32_read(base, ring_desc_off + offsetof(struct armaccel_event_ring_v1, capacity));
        session->event_ring_event_size =
            svc_be32_read(base, ring_desc_off + offsetof(struct armaccel_event_ring_v1, event_size));
        if (session->event_ring_event_size < ARMACCEL_SVC_DEFAULT_EVENT_SIZE) {
          session->event_ring_event_size = ARMACCEL_SVC_DEFAULT_EVENT_SIZE;
        }
        session->event_ring_head = 0u;
        session->event_ring_tail = 0u;
        session->event_ring_overflow_count = 0u;
        svc_be32_write(base, ring_desc_off + offsetof(struct armaccel_event_ring_v1, head), 0u);
        svc_be32_write(base, ring_desc_off + offsetof(struct armaccel_event_ring_v1, tail), 0u);
        svc_be32_write(base, ring_desc_off + offsetof(struct armaccel_event_ring_v1, overflow_count), 0u);
      }
    }

    caps_off = svc_be32_read(base, req_off + offsetof(struct armaccel_session_open_v1, caps_out_offset));
    if (caps_off != 0u) {
      svc_write_caps(base, caps_off);
    }
    *out_result0 = ARMACCEL_SVC_RES_OK;
    *out_result1 = session->handle;
    return;
  }

  if (opcode == ARMACCEL_SVC_SESSION_CLOSE) {
    struct armaccel_service_session *session = svc_find_session(rt, arg0);
    if (session == NULL) {
      *out_result0 = ARMACCEL_SVC_RES_BAD_HANDLE;
      return;
    }
    svc_close_session_children(rt, session->handle);
    memset(session, 0, sizeof(*session));
    *out_result0 = ARMACCEL_SVC_RES_OK;
    return;
  }

  if (opcode == ARMACCEL_SVC_SESSION_GET_CAPS) {
    if (!svc_bounds_ok(arg0, (ULONG)sizeof(struct armaccel_caps_v1))) {
      *out_result0 = ARMACCEL_SVC_RES_INVALID_ARG;
      return;
    }
    svc_write_caps(base, arg0);
    *out_result0 = ARMACCEL_SVC_RES_OK;
    return;
  }
}

static void svc_handle_window(volatile UBYTE *base, struct armaccel_service_runtime *rt, ULONG opcode,
                              ULONG arg0, ULONG *out_result0, ULONG *out_result1) {
  ULONG i;

  *out_result0 = ARMACCEL_SVC_RES_UNSUPPORTED;
  *out_result1 = 0u;

  if (opcode == ARMACCEL_SVC_WINDOW_OPEN) {
    ULONG req_off = arg0;
    ULONG session_handle;
    LONG x;
    LONG y;
    ULONG width;
    ULONG height;
    ULONG title_off;
    ULONG title_size;
    ULONG window_flags;
    ULONG idcmp_mask;
    ULONG intuition_flags;
    struct armaccel_service_window *win_state = NULL;
    struct Window *win;
    char *title_buf;

    if (!svc_bounds_ok(req_off, (ULONG)sizeof(struct armaccel_window_open_v1))) {
      *out_result0 = ARMACCEL_SVC_RES_INVALID_ARG;
      return;
    }
    session_handle =
        svc_be32_read(base, req_off + offsetof(struct armaccel_window_open_v1, session_handle));
    if (svc_find_session(rt, session_handle) == NULL) {
      *out_result0 = ARMACCEL_SVC_RES_BAD_HANDLE;
      return;
    }
    for (i = 0u; i < ARMACCEL_SVC_MAX_WINDOWS; i++) {
      if (rt->windows[i].in_use == 0u) {
        win_state = &rt->windows[i];
        break;
      }
    }
    if (win_state == NULL) {
      *out_result0 = ARMACCEL_SVC_RES_BUSY;
      return;
    }

    x = (LONG)svc_be32_read(base, req_off + offsetof(struct armaccel_window_open_v1, x));
    y = (LONG)svc_be32_read(base, req_off + offsetof(struct armaccel_window_open_v1, y));
    width = svc_be32_read(base, req_off + offsetof(struct armaccel_window_open_v1, width));
    height = svc_be32_read(base, req_off + offsetof(struct armaccel_window_open_v1, height));
    title_off = svc_be32_read(base, req_off + offsetof(struct armaccel_window_open_v1, title_offset));
    title_size = svc_be32_read(base, req_off + offsetof(struct armaccel_window_open_v1, title_size));
    window_flags =
        svc_be32_read(base, req_off + offsetof(struct armaccel_window_open_v1, window_flags));
    idcmp_mask = svc_be32_read(base, req_off + offsetof(struct armaccel_window_open_v1, idcmp_mask));

    if (width == 0u) {
      width = 320u;
    }
    if (height == 0u) {
      height = 200u;
    }
    title_buf = svc_copy_string(base, title_off, title_size);
    if (title_buf == NULL) {
      title_buf = (char *)"ARMAccel";
    }

    intuition_flags = 0u;
    if ((window_flags & ARMACCEL_WINDOW_FLAG_CLOSEGADGET) != 0u) {
      intuition_flags |= WFLG_CLOSEGADGET;
    }
    if ((window_flags & ARMACCEL_WINDOW_FLAG_DRAGBAR) != 0u) {
      intuition_flags |= WFLG_DRAGBAR;
    }
    if ((window_flags & ARMACCEL_WINDOW_FLAG_DEPTHGADGET) != 0u) {
      intuition_flags |= WFLG_DEPTHGADGET;
    }
    if ((window_flags & ARMACCEL_WINDOW_FLAG_SIZEGADGET) != 0u) {
      intuition_flags |= WFLG_SIZEGADGET;
    }
    if ((window_flags & ARMACCEL_WINDOW_FLAG_ACTIVATE) != 0u) {
      intuition_flags |= WFLG_ACTIVATE;
    }
    win = OpenWindowTags(NULL, WA_Left, x, WA_Top, y, WA_Width, width, WA_Height, height, WA_Title,
                         (ULONG)title_buf, WA_Flags, intuition_flags, WA_IDCMP, idcmp_mask, TAG_DONE);
    if ((title_buf != NULL) && (title_buf != (char *)"ARMAccel")) {
      free(title_buf);
    }
    if (win == NULL) {
      *out_result0 = ARMACCEL_SVC_RES_IO;
      *out_result1 = ARMACCEL_SVC_ERR_PACK(ARMACCEL_SVC_ERRNS_INTUITION, 1u);
      return;
    }

    memset(win_state, 0, sizeof(*win_state));
    win_state->in_use = 1u;
    win_state->handle = svc_next_handle(rt);
    win_state->session_handle = session_handle;
    win_state->intuition_window = win;
    *out_result0 = ARMACCEL_SVC_RES_OK;
    *out_result1 = win_state->handle;
    return;
  }

  if (opcode == ARMACCEL_SVC_WINDOW_CLOSE) {
    struct armaccel_service_window *win_state = svc_find_window(rt, arg0);
    if (win_state == NULL) {
      *out_result0 = ARMACCEL_SVC_RES_BAD_HANDLE;
      return;
    }
    if (win_state->intuition_window != NULL) {
      CloseWindow(win_state->intuition_window);
    }
    memset(win_state, 0, sizeof(*win_state));
    *out_result0 = ARMACCEL_SVC_RES_OK;
    return;
  }
}

static void svc_handle_surface(volatile UBYTE *base, struct armaccel_service_runtime *rt, ULONG opcode,
                               ULONG arg0, ULONG *out_result0, ULONG *out_result1) {
  ULONG i;
  (void)base;
  *out_result0 = ARMACCEL_SVC_RES_UNSUPPORTED;
  *out_result1 = 0u;

  if (opcode == ARMACCEL_SVC_SURFACE_ALLOC) {
    ULONG req_off = arg0;
    ULONG session_handle;
    struct armaccel_service_surface *surface = NULL;
    if (!svc_bounds_ok(req_off, (ULONG)sizeof(struct armaccel_surface_desc_v1))) {
      *out_result0 = ARMACCEL_SVC_RES_INVALID_ARG;
      return;
    }
    session_handle =
        svc_be32_read(base, req_off + offsetof(struct armaccel_surface_desc_v1, session_handle));
    if (svc_find_session(rt, session_handle) == NULL) {
      *out_result0 = ARMACCEL_SVC_RES_BAD_HANDLE;
      return;
    }
    for (i = 0u; i < ARMACCEL_SVC_MAX_SURFACES; i++) {
      if (rt->surfaces[i].in_use == 0u) {
        surface = &rt->surfaces[i];
        break;
      }
    }
    if (surface == NULL) {
      *out_result0 = ARMACCEL_SVC_RES_BUSY;
      return;
    }

    memset(surface, 0, sizeof(*surface));
    surface->in_use = 1u;
    surface->handle = svc_next_handle(rt);
    surface->session_handle = session_handle;
    surface->window_handle =
        svc_be32_read(base, req_off + offsetof(struct armaccel_surface_desc_v1, attach_window_handle));
    surface->pixel_format =
        svc_be32_read(base, req_off + offsetof(struct armaccel_surface_desc_v1, pixel_format));
    surface->width = svc_be32_read(base, req_off + offsetof(struct armaccel_surface_desc_v1, width));
    surface->height = svc_be32_read(base, req_off + offsetof(struct armaccel_surface_desc_v1, height));
    surface->stride = svc_be32_read(base, req_off + offsetof(struct armaccel_surface_desc_v1, stride));
    surface->buffer_offset =
        svc_be32_read(base, req_off + offsetof(struct armaccel_surface_desc_v1, buffer_offset));
    surface->buffer_size =
        svc_be32_read(base, req_off + offsetof(struct armaccel_surface_desc_v1, buffer_size));

    *out_result0 = ARMACCEL_SVC_RES_OK;
    *out_result1 = surface->handle;
    return;
  }

  if (opcode == ARMACCEL_SVC_SURFACE_PRESENT) {
    if (svc_find_surface(rt, arg0) == NULL) {
      *out_result0 = ARMACCEL_SVC_RES_BAD_HANDLE;
      return;
    }
    *out_result0 = ARMACCEL_SVC_RES_OK;
    return;
  }

  if (opcode == ARMACCEL_SVC_SURFACE_FREE) {
    struct armaccel_service_surface *surface = svc_find_surface(rt, arg0);
    if (surface == NULL) {
      *out_result0 = ARMACCEL_SVC_RES_BAD_HANDLE;
      return;
    }
    memset(surface, 0, sizeof(*surface));
    *out_result0 = ARMACCEL_SVC_RES_OK;
    return;
  }
}

static void svc_handle_input(volatile UBYTE *base, struct armaccel_service_runtime *rt, ULONG opcode,
                             ULONG arg0, ULONG *out_result0, ULONG *out_result1) {
  struct armaccel_service_session *session;
  ULONG i;
  (void)base;

  *out_result0 = ARMACCEL_SVC_RES_UNSUPPORTED;
  *out_result1 = 0u;
  if (opcode != ARMACCEL_SVC_INPUT_POLL) {
    return;
  }

  session = svc_find_session(rt, arg0);
  if (session == NULL) {
    *out_result0 = ARMACCEL_SVC_RES_BAD_HANDLE;
    return;
  }

  for (i = 0u; i < ARMACCEL_SVC_MAX_WINDOWS; i++) {
    if ((rt->windows[i].in_use != 0u) && (rt->windows[i].session_handle == session->handle) &&
        (rt->windows[i].intuition_window != NULL) && (rt->windows[i].intuition_window->UserPort != NULL)) {
      struct IntuiMessage *msg =
          (struct IntuiMessage *)GetMsg(rt->windows[i].intuition_window->UserPort);
      if (msg != NULL) {
        ULONG evt_code = 0u;
        ULONG d0 = msg->Code;
        ULONG d1 = (ULONG)(UWORD)msg->MouseX;
        ULONG d2 = (ULONG)(UWORD)msg->MouseY;

        if (msg->Class == IDCMP_CLOSEWINDOW) {
          evt_code = ARMACCEL_EVT_WINDOW_CLOSE;
        } else if (msg->Class == IDCMP_NEWSIZE) {
          evt_code = ARMACCEL_EVT_WINDOW_RESIZE;
        } else if (msg->Class == IDCMP_MOUSEMOVE) {
          evt_code = ARMACCEL_EVT_INPUT_MOUSEMOVE;
        } else if (msg->Class == IDCMP_MOUSEBUTTONS) {
          evt_code = ARMACCEL_EVT_INPUT_BUTTON;
        } else if (msg->Class == IDCMP_RAWKEY) {
          evt_code = ARMACCEL_EVT_INPUT_KEYDOWN;
        }

        if (evt_code != 0u) {
          svc_push_event(base, session,
                         (msg->Class == IDCMP_CLOSEWINDOW || msg->Class == IDCMP_NEWSIZE)
                             ? ARMACCEL_EVT_WINDOW
                             : ARMACCEL_EVT_INPUT,
                         evt_code, rt->windows[i].handle, d0, d1, d2, 0u);
        }
        ReplyMsg((struct Message *)msg);
        *out_result0 = ARMACCEL_SVC_RES_OK;
        *out_result1 = 1u;
        return;
      }
    }
  }

  *out_result0 = ARMACCEL_SVC_RES_OK;
  *out_result1 = 0u;
}

static void svc_handle_requester(volatile UBYTE *base, struct armaccel_service_runtime *rt, ULONG opcode,
                                 ULONG arg0, ULONG *out_result0, ULONG *out_result1) {
  ULONG req_off;
  ULONG session_handle;
  ULONG window_handle;
  ULONG requester_type;
  ULONG text_offset;
  ULONG text_size;
  struct armaccel_service_window *win_state;
  struct EasyStruct es;
  char *text_buf;
  LONG req_res;

  *out_result0 = ARMACCEL_SVC_RES_UNSUPPORTED;
  *out_result1 = 0u;

  if (opcode != ARMACCEL_SVC_REQUESTER_OPEN) {
    return;
  }

  req_off = arg0;
  if (!svc_bounds_ok(req_off, (ULONG)sizeof(struct armaccel_requester_open_v1))) {
    *out_result0 = ARMACCEL_SVC_RES_INVALID_ARG;
    return;
  }

  session_handle =
      svc_be32_read(base, req_off + offsetof(struct armaccel_requester_open_v1, session_handle));
  if (svc_find_session(rt, session_handle) == NULL) {
    *out_result0 = ARMACCEL_SVC_RES_BAD_HANDLE;
    return;
  }
  window_handle =
      svc_be32_read(base, req_off + offsetof(struct armaccel_requester_open_v1, window_handle));
  requester_type =
      svc_be32_read(base, req_off + offsetof(struct armaccel_requester_open_v1, requester_type));
  text_offset = svc_be32_read(base, req_off + offsetof(struct armaccel_requester_open_v1, text_offset));
  text_size = svc_be32_read(base, req_off + offsetof(struct armaccel_requester_open_v1, text_size));

  if (requester_type != ARMACCEL_REQUESTER_MESSAGE) {
    *out_result0 = ARMACCEL_SVC_RES_UNSUPPORTED;
    return;
  }

  text_buf = svc_copy_string(base, text_offset, text_size);
  if (text_buf == NULL) {
    *out_result0 = ARMACCEL_SVC_RES_INVALID_ARG;
    return;
  }

  memset(&es, 0, sizeof(es));
  es.es_StructSize = sizeof(es);
  es.es_Title = (UBYTE *)"ARMAccel";
  es.es_TextFormat = (UBYTE *)text_buf;
  es.es_GadgetFormat = (UBYTE *)"OK";

  win_state = svc_find_window(rt, window_handle);
  req_res = EasyRequestArgs((win_state != NULL) ? win_state->intuition_window : NULL, &es, NULL, NULL);
  free(text_buf);

  *out_result0 = ARMACCEL_SVC_RES_OK;
  *out_result1 = (req_res != 0) ? 1u : 0u;
}

static void svc_handle_file(volatile UBYTE *base, struct armaccel_service_runtime *rt, ULONG opcode,
                            ULONG arg0, ULONG *out_result0, ULONG *out_result1) {
  ULONG i;

  *out_result0 = ARMACCEL_SVC_RES_UNSUPPORTED;
  *out_result1 = 0u;

  if (opcode == ARMACCEL_SVC_FILE_OPEN) {
    ULONG req_off = arg0;
    ULONG session_handle;
    ULONG open_flags;
    ULONG path_off;
    ULONG path_size;
    char *path;
    LONG mode;
    BPTR fh;
    struct armaccel_service_file *slot = NULL;

    if (!svc_bounds_ok(req_off, (ULONG)sizeof(struct armaccel_file_open_v1))) {
      *out_result0 = ARMACCEL_SVC_RES_INVALID_ARG;
      return;
    }
    session_handle =
        svc_be32_read(base, req_off + offsetof(struct armaccel_file_open_v1, session_handle));
    if (svc_find_session(rt, session_handle) == NULL) {
      *out_result0 = ARMACCEL_SVC_RES_BAD_HANDLE;
      return;
    }
    open_flags = svc_be32_read(base, req_off + offsetof(struct armaccel_file_open_v1, open_flags));
    path_off = svc_be32_read(base, req_off + offsetof(struct armaccel_file_open_v1, path_offset));
    path_size = svc_be32_read(base, req_off + offsetof(struct armaccel_file_open_v1, path_size));
    path = svc_copy_string(base, path_off, path_size);
    if (path == NULL) {
      *out_result0 = ARMACCEL_SVC_RES_INVALID_ARG;
      return;
    }

    mode = ((open_flags & ARMACCEL_SVC_FILE_OPENFLAG_WRITE) != 0u) ? MODE_NEWFILE : MODE_OLDFILE;
    fh = Open((CONST_STRPTR)path, mode);
    free(path);
    if (fh == (BPTR)0) {
      *out_result0 = ARMACCEL_SVC_RES_IO;
      *out_result1 = ARMACCEL_SVC_ERR_PACK(ARMACCEL_SVC_ERRNS_DOS, IoErr());
      return;
    }
    for (i = 0u; i < ARMACCEL_SVC_MAX_FILES; i++) {
      if (rt->files[i].in_use == 0u) {
        slot = &rt->files[i];
        break;
      }
    }
    if (slot == NULL) {
      Close(fh);
      *out_result0 = ARMACCEL_SVC_RES_BUSY;
      return;
    }
    memset(slot, 0, sizeof(*slot));
    slot->in_use = 1u;
    slot->handle = svc_next_handle(rt);
    slot->session_handle = session_handle;
    slot->file_handle = fh;
    *out_result0 = ARMACCEL_SVC_RES_OK;
    *out_result1 = slot->handle;
    return;
  }

  if (opcode == ARMACCEL_SVC_FILE_READ) {
    ULONG req_off = arg0;
    ULONG file_handle;
    ULONG buf_off;
    ULONG buf_size;
    struct armaccel_service_file *file_slot;
    LONG read_rc;

    if (!svc_bounds_ok(req_off, (ULONG)sizeof(struct armaccel_file_io_v1))) {
      *out_result0 = ARMACCEL_SVC_RES_INVALID_ARG;
      return;
    }
    file_handle = svc_be32_read(base, req_off + offsetof(struct armaccel_file_io_v1, file_handle));
    buf_off = svc_be32_read(base, req_off + offsetof(struct armaccel_file_io_v1, buffer_offset));
    buf_size = svc_be32_read(base, req_off + offsetof(struct armaccel_file_io_v1, buffer_size));

    file_slot = svc_find_file(rt, file_handle);
    if ((file_slot == NULL) || (file_slot->file_handle == (BPTR)0)) {
      *out_result0 = ARMACCEL_SVC_RES_BAD_HANDLE;
      return;
    }
    if (!svc_bounds_ok(buf_off, buf_size)) {
      *out_result0 = ARMACCEL_SVC_RES_INVALID_ARG;
      return;
    }
    read_rc = Read(file_slot->file_handle, (APTR)(base + buf_off), (LONG)buf_size);
    if (read_rc < 0) {
      *out_result0 = ARMACCEL_SVC_RES_IO;
      *out_result1 = ARMACCEL_SVC_ERR_PACK(ARMACCEL_SVC_ERRNS_DOS, IoErr());
      return;
    }
    *out_result0 = ARMACCEL_SVC_RES_OK;
    *out_result1 = (ULONG)read_rc;
    return;
  }

  if (opcode == ARMACCEL_SVC_FILE_CLOSE) {
    struct armaccel_service_file *file_slot = svc_find_file(rt, arg0);
    if ((file_slot == NULL) || (file_slot->file_handle == (BPTR)0)) {
      *out_result0 = ARMACCEL_SVC_RES_BAD_HANDLE;
      return;
    }
    Close(file_slot->file_handle);
    memset(file_slot, 0, sizeof(*file_slot));
    *out_result0 = ARMACCEL_SVC_RES_OK;
    return;
  }
}

static void svc_handle_timer(ULONG opcode, ULONG arg0, ULONG *out_result0, ULONG *out_result1) {
  struct DateStamp ds;
  ULONG tick;

  *out_result0 = ARMACCEL_SVC_RES_UNSUPPORTED;
  *out_result1 = 0u;

  if (opcode == ARMACCEL_SVC_TIMER_SLEEP_MS) {
    ULONG ticks = (arg0 + 19u) / 20u;
    Delay((LONG)ticks);
    *out_result0 = ARMACCEL_SVC_RES_OK;
    return;
  }
  if (opcode == ARMACCEL_SVC_TIMER_GET_TICK) {
    DateStamp(&ds);
    tick = (ULONG)ds.ds_Tick + ((ULONG)ds.ds_Minute * 3000u);
    tick += ((ULONG)ds.ds_Days * 24u * 60u * 3000u);
    *out_result0 = ARMACCEL_SVC_RES_OK;
    *out_result1 = tick;
    return;
  }
}

static ULONG armaccel_service_dispatch_hook(volatile UBYTE *board_base, APTR context) {
  struct armaccel_service_runtime *rt = (struct armaccel_service_runtime *)context;
  ULONG frame_off;
  ULONG magic;
  ULONG version;
  ULONG state;
  ULONG service;
  ULONG opcode;
  ULONG arg0;
  ULONG arg1;
  ULONG result0 = ARMACCEL_SVC_RES_UNSUPPORTED;
  ULONG result1 = 0u;

  if ((board_base == NULL) || (rt == NULL)) {
    return 1u;
  }

  frame_off = ARM64_ACCEL_JOBDESC_OFFSET + ARMACCEL_SVC_FRAME_JOBDESC_OFF;
  magic = svc_be32_read(board_base, frame_off + ARMACCEL_SVC_OFF_MAGIC);
  version = svc_be32_read(board_base, frame_off + ARMACCEL_SVC_OFF_VERSION);
  state = svc_be32_read(board_base, frame_off + ARMACCEL_SVC_OFF_STATE);

  if ((magic != ARMACCEL_SVC_FRAME_MAGIC) || (version != ARMACCEL_SVC_FRAME_VERSION) ||
      (state != ARMACCEL_SVC_STATE_PENDING)) {
    return 0u;
  }

  service = svc_be32_read(board_base, frame_off + ARMACCEL_SVC_OFF_SERVICE);
  opcode = svc_be32_read(board_base, frame_off + ARMACCEL_SVC_OFF_OPCODE);
  arg0 = svc_be32_read(board_base, frame_off + ARMACCEL_SVC_OFF_ARG0);
  arg1 = svc_be32_read(board_base, frame_off + ARMACCEL_SVC_OFF_ARG1);

  if (service == ARMACCEL_SVC_SESSION) {
    svc_handle_session(board_base, rt, opcode, arg0, arg1, &result0, &result1);
  } else if (service == ARMACCEL_SVC_WINDOW) {
    svc_handle_window(board_base, rt, opcode, arg0, &result0, &result1);
  } else if (service == ARMACCEL_SVC_SURFACE) {
    svc_handle_surface(board_base, rt, opcode, arg0, &result0, &result1);
  } else if (service == ARMACCEL_SVC_INPUT) {
    svc_handle_input(board_base, rt, opcode, arg0, &result0, &result1);
  } else if (service == ARMACCEL_SVC_REQUESTER) {
    svc_handle_requester(board_base, rt, opcode, arg0, &result0, &result1);
  } else if (service == ARMACCEL_SVC_FILE) {
    svc_handle_file(board_base, rt, opcode, arg0, &result0, &result1);
  } else if (service == ARMACCEL_SVC_TIMER) {
    svc_handle_timer(opcode, arg0, &result0, &result1);
  } else {
    result0 = ARMACCEL_SVC_RES_UNSUPPORTED;
    result1 = ARMACCEL_SVC_ERR_PACK(ARMACCEL_SVC_ERRNS_GENERIC, 1u);
  }

  svc_frame_finish(board_base, result0, result1, (result0 == ARMACCEL_SVC_RES_OK) ? 0u : 1u);
  return 0u;
}

LONG ARMACCEL_QueryELF(const char *path, struct ArmAccelELFInfo *out_info) {
  unsigned char *elf_bytes;
  ULONG elf_size;
  struct elf_header_info eh;
  int rc;

  if (out_info == NULL) {
    return -1;
  }

  memset(out_info, 0, sizeof(*out_info));
  out_info->available_services = ARMACCEL_AVAILABLE_SERVICES;
  out_info->compatibility = ARMACCEL_ELF_COMPAT_INVALID_FILE;

  rc = load_file_bytes(path, &elf_bytes, &elf_size);
  if (rc != 0) {
    return (LONG)out_info->compatibility;
  }
  out_info->elf_size = elf_size;

  rc = validate_elf64_aarch64(elf_bytes, elf_size, &eh);
  if (rc != 0) {
    free(elf_bytes);
    return (LONG)out_info->compatibility;
  }

  rc = parse_armaccel_note(elf_bytes, &eh, out_info);
  free(elf_bytes);
  if (rc < 0) {
    out_info->compatibility = ARMACCEL_ELF_COMPAT_INVALID_FILE;
    return (LONG)out_info->compatibility;
  }
  if (rc > 0) {
    out_info->compatibility = ARMACCEL_ELF_COMPAT_NOT_ARMACCEL;
    return (LONG)out_info->compatibility;
  }

  if (out_info->abi_major != ARMACCEL_LIBRARY_SUPPORTED_ABI_MAJOR) {
    out_info->compatibility = ARMACCEL_ELF_COMPAT_WRONG_ABI;
    return (LONG)out_info->compatibility;
  }

  if ((out_info->required_services & ~out_info->available_services) != 0u) {
    out_info->compatibility = ARMACCEL_ELF_COMPAT_NEEDS_SERVICES;
    return (LONG)out_info->compatibility;
  }

  out_info->compatibility = ARMACCEL_ELF_COMPAT_RUNNABLE;
  return (LONG)out_info->compatibility;
}

LONG ARMACCEL_IsSupportedELF(const char *path) {
  struct ArmAccelELFInfo info;
  LONG rc = ARMACCEL_QueryELF(path, &info);
  return (rc == (LONG)ARMACCEL_ELF_COMPAT_RUNNABLE) ? 1 : 0;
}

LONG ARMACCEL_ExecuteELF(const char *path, const struct ArmAccelRunOpts *opts,
                         struct ArmAccelResult *out_result) {
  struct ArmAccelELFInfo info;
  struct MsgPort *port;
  struct ArmAccelIORequest *req;
  struct armaccel_service_runtime svc_runtime;
  ULONG result0 = 0u;
  ULONG result1 = 0u;
  ULONG job_state = 0u;
  ULONG job_result = 0u;
  ULONG ret_lo = 0u;
  ULONG ret_hi = 0u;
  ULONG service_dispatch_count = 0u;
  ULONG service_hook_status = 0u;
  int rc;
  LONG io_status;
  LONG exec_rc = ARMACCEL_EXEC_ERR_RUN;

  (void)opts;

  if (path == NULL) {
    return ARMACCEL_EXEC_ERR_INVALID_ARG;
  }

  if (out_result != NULL) {
    memset(out_result, 0, sizeof(*out_result));
  }

  rc = (int)ARMACCEL_QueryELF(path, &info);
  if (out_result != NULL) {
    out_result->compatibility = info.compatibility;
  }

  if ((rc == (int)ARMACCEL_ELF_COMPAT_NOT_ARMACCEL) ||
      (rc == (int)ARMACCEL_ELF_COMPAT_INVALID_FILE)) {
    return ARMACCEL_EXEC_ERR_NOT_ARMACCEL;
  }
  if (rc == (int)ARMACCEL_ELF_COMPAT_WRONG_ABI) {
    return ARMACCEL_EXEC_ERR_WRONG_ABI;
  }
  if (rc == (int)ARMACCEL_ELF_COMPAT_NEEDS_SERVICES) {
    return ARMACCEL_EXEC_ERR_NEEDS_SERVICES;
  }
  if (rc != (int)ARMACCEL_ELF_COMPAT_RUNNABLE) {
    return ARMACCEL_EXEC_ERR_NOT_ARMACCEL;
  }

  svc_runtime_init(&svc_runtime);

  port = NULL;
  req = NULL;
  rc = open_armaccel_device(&port, &req);
  if (rc != 0) {
    return ARMACCEL_EXEC_ERR_DEVICE_OPEN;
  }

  req->payload_size = 0u;
  req->transport_status = 0u;
  req->result0 = 0u;
  req->result1 = 0u;
  req->job_state = 0u;
  req->job_result = 0u;
  req->retval_lo = 0u;
  req->retval_hi = 0u;
  req->service_hook = NULL;
  req->service_ctx = NULL;
  req->service_dispatch_count = 0u;
  req->service_hook_status = 0u;
  req->io.io_Command = ARMACCEL_CMD_UPLOAD_ELF_PATH;
  req->io.io_Flags = IOF_QUICK;
  req->path = (STRPTR)path;
  io_status = DoIO((struct IORequest *)req);
  if ((io_status != 0) || (req->io.io_Error != 0)) {
    exec_rc = ARMACCEL_EXEC_ERR_UPLOAD;
    goto done;
  }

  req->payload_size = 0u;
  req->transport_status = 0u;
  req->result0 = 0u;
  req->result1 = 0u;
  req->job_state = 0u;
  req->job_result = 0u;
  req->retval_lo = 0u;
  req->retval_hi = 0u;
  req->service_hook = armaccel_service_dispatch_hook;
  req->service_ctx = &svc_runtime;
  req->service_dispatch_count = 0u;
  req->service_hook_status = 0u;
  req->path = NULL;
  req->io.io_Command = ARMACCEL_CMD_RUN_ELF_JOB;
  req->io.io_Flags = IOF_QUICK;
  io_status = DoIO((struct IORequest *)req);
  if ((io_status != 0) || (req->io.io_Error != 0)) {
    exec_rc = ARMACCEL_EXEC_ERR_RUN;
    goto done;
  }

  result0 = req->result0;
  result1 = req->result1;
  job_state = req->job_state;
  job_result = req->job_result;
  ret_lo = req->retval_lo;
  ret_hi = req->retval_hi;
  service_dispatch_count = req->service_dispatch_count;
  service_hook_status = req->service_hook_status;

  exec_rc = ARMACCEL_EXEC_OK;

done:
  if (req != NULL) {
    req->service_hook = NULL;
    req->service_ctx = NULL;
  }
  svc_runtime_cleanup(&svc_runtime);
  close_armaccel_device(port, req);

  if (exec_rc != ARMACCEL_EXEC_OK) {
    return exec_rc;
  }

  if (out_result != NULL) {
    out_result->compatibility = ARMACCEL_ELF_COMPAT_RUNNABLE;
    out_result->device_result0 = result0;
    out_result->device_result1 = result1;
    out_result->job_state = job_state;
    out_result->job_result = job_result;
    out_result->retval_lo = ret_lo;
    out_result->retval_hi = ret_hi;
    out_result->service_dispatch_count = service_dispatch_count;
    out_result->service_hook_status = service_hook_status;
  }
  return exec_rc;
}
