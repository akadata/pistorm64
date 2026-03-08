// SPDX-License-Identifier: MIT
#include <stdint.h>
#include <stddef.h>

#include "arm64_accel_regs.h"
#include "armaccel_service_abi.h"

#define ARMACCEL_NOTE_TYPE 0x4143434Cu /* "ACCL" */
#define ARMACCEL_NOTE_DESC_MAGIC 0x414E5631u /* "ANV1" */
#define ARMACCEL_PERSONALITY_ARM64 1u
#define ARMACCEL_APPCLASS_TOOL 4u

#define WORK_OFF         0x00300000u
#define WORK_RING_DESC   (WORK_OFF + 0x000u)
#define WORK_SESSION_REQ (WORK_OFF + 0x040u)
#define WORK_WINDOW_REQ  (WORK_OFF + 0x080u)
#define WORK_FILE_REQ    (WORK_OFF + 0x0C0u)
#define WORK_FILE_IO_REQ (WORK_OFF + 0x100u)
#define WORK_REQ_REQ     (WORK_OFF + 0x140u)
#define WORK_CAPS_OUT    (WORK_OFF + 0x200u)
#define WORK_EVENTS      (WORK_OFF + 0x300u)
#define WORK_TITLE_STR   (WORK_OFF + 0x600u)
#define WORK_TEXT_STR    (WORK_OFF + 0x640u)
#define WORK_PATH_STR    (WORK_OFF + 0x680u)
#define WORK_FILE_BUF    (WORK_OFF + 0x700u)

#define STEP_TIMEOUT 20000000u

__attribute__((section(".note.armaccel"), used, aligned(8))) static const uint8_t g_armaccel_note[] = {
    /* Elf64_Nhdr: namesz=8, descsz=24, type='ACCL' */
    0x08, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x4C, 0x43, 0x43, 0x41,
    /* name: \"ARMACCEL\" */
    'A',  'R',  'M',  'A',  'C',  'C',  'E',  'L',
    /* desc: magic='ANV1' (LE), version=1 (u16), abi_major=1 (u16) */
    0x31, 0x56, 0x4E, 0x41, 0x01, 0x00, 0x01, 0x00,
    /* personality=ARM64 */
    0x01, 0x00, 0x00, 0x00,
    /* required services mask (WINDOW|REQUESTER|INPUT|FILE|TIMER = 0x00000075) */
    0x75, 0x00, 0x00, 0x00,
    /* app_class=TOOL, reserved0=0 */
    0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* align note payload to 8-byte boundary */
    0x00, 0x00, 0x00, 0x00,
};

static uint32_t be32_read(const uint8_t *base, uint32_t off) {
  return ((uint32_t)base[off + 0u] << 24u) | ((uint32_t)base[off + 1u] << 16u) |
         ((uint32_t)base[off + 2u] << 8u) | (uint32_t)base[off + 3u];
}

static void be32_write(uint8_t *base, uint32_t off, uint32_t value) {
  base[off + 0u] = (uint8_t)((value >> 24u) & 0xFFu);
  base[off + 1u] = (uint8_t)((value >> 16u) & 0xFFu);
  base[off + 2u] = (uint8_t)((value >> 8u) & 0xFFu);
  base[off + 3u] = (uint8_t)(value & 0xFFu);
}

static void write_cstr(uint8_t *base, uint32_t off, const char *s) {
  uint32_t i = 0u;
  while (s[i] != '\0') {
    base[off + i] = (uint8_t)s[i];
    i++;
  }
  base[off + i] = 0;
}

static int svc_call(uint8_t *base, uint8_t *job, uint32_t service, uint32_t opcode, uint32_t arg0,
                    uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t *out_r0,
                    uint32_t *out_r1) {
  uint32_t frame = ARM64_ACCEL_JOBDESC_OFFSET + ARMACCEL_SVC_FRAME_JOBDESC_OFF;
  uint32_t spin;

  be32_write(base, frame + ARMACCEL_SVC_OFF_MAGIC, ARMACCEL_SVC_FRAME_MAGIC);
  be32_write(base, frame + ARMACCEL_SVC_OFF_VERSION, ARMACCEL_SVC_FRAME_VERSION);
  be32_write(base, frame + ARMACCEL_SVC_OFF_SERVICE, service);
  be32_write(base, frame + ARMACCEL_SVC_OFF_OPCODE, opcode);
  be32_write(base, frame + ARMACCEL_SVC_OFF_ARG0, arg0);
  be32_write(base, frame + ARMACCEL_SVC_OFF_ARG1, arg1);
  be32_write(base, frame + ARMACCEL_SVC_OFF_ARG2, arg2);
  be32_write(base, frame + ARMACCEL_SVC_OFF_ARG3, arg3);
  be32_write(base, frame + ARMACCEL_SVC_OFF_RESULT0, 0u);
  be32_write(base, frame + ARMACCEL_SVC_OFF_RESULT1, 0u);
  be32_write(base, frame + ARMACCEL_SVC_OFF_STATE, ARMACCEL_SVC_STATE_PENDING);

  for (spin = 0u; spin < STEP_TIMEOUT; spin++) {
    uint32_t state = be32_read(base, frame + ARMACCEL_SVC_OFF_STATE);
    if ((state == ARMACCEL_SVC_STATE_DONE) || (state == ARMACCEL_SVC_STATE_ERROR)) {
      if (out_r0 != 0) {
        *out_r0 = be32_read(base, frame + ARMACCEL_SVC_OFF_RESULT0);
      }
      if (out_r1 != 0) {
        *out_r1 = be32_read(base, frame + ARMACCEL_SVC_OFF_RESULT1);
      }
      return (state == ARMACCEL_SVC_STATE_DONE) ? 0 : 1;
    }
  }

  (void)job;
  return 2;
}

uint64_t arm_job_entry(void *job_ptr) {
  uint8_t *job = (uint8_t *)job_ptr;
  uint8_t *base = job - ARM64_ACCEL_JOBDESC_OFFSET;
  uint32_t r0 = 0u;
  uint32_t r1 = 0u;
  uint32_t session = 0u;
  uint32_t window = 0u;
  uint32_t file = 0u;
  uint32_t ok_count = 0u;

  /* Event ring descriptor */
  be32_write(base, WORK_RING_DESC + offsetof(struct armaccel_event_ring_v1, offset), WORK_EVENTS);
  be32_write(base, WORK_RING_DESC + offsetof(struct armaccel_event_ring_v1, capacity), 8u);
  be32_write(base, WORK_RING_DESC + offsetof(struct armaccel_event_ring_v1, event_size),
             sizeof(struct armaccel_event_v1));
  be32_write(base, WORK_RING_DESC + offsetof(struct armaccel_event_ring_v1, head), 0u);
  be32_write(base, WORK_RING_DESC + offsetof(struct armaccel_event_ring_v1, tail), 0u);
  be32_write(base, WORK_RING_DESC + offsetof(struct armaccel_event_ring_v1, flags), 0u);
  be32_write(base, WORK_RING_DESC + offsetof(struct armaccel_event_ring_v1, overflow_count), 0u);

  /* SESSION_OPEN */
  be32_write(base, WORK_SESSION_REQ + offsetof(struct armaccel_session_open_v1, requested_service_caps),
             ARMACCEL_CAP_WINDOW | ARMACCEL_CAP_REQUESTER | ARMACCEL_CAP_INPUT | ARMACCEL_CAP_FILE |
                 ARMACCEL_CAP_TIMER);
  be32_write(base, WORK_SESSION_REQ + offsetof(struct armaccel_session_open_v1, requested_profile_caps),
             ARMACCEL_PROFILE_APP);
  be32_write(base, WORK_SESSION_REQ + offsetof(struct armaccel_session_open_v1, event_ring_desc_offset),
             WORK_RING_DESC);
  be32_write(base, WORK_SESSION_REQ + offsetof(struct armaccel_session_open_v1, event_ring_desc_size),
             sizeof(struct armaccel_event_ring_v1));
  be32_write(base, WORK_SESSION_REQ + offsetof(struct armaccel_session_open_v1, caps_out_offset),
             WORK_CAPS_OUT);
  be32_write(base, WORK_SESSION_REQ + offsetof(struct armaccel_session_open_v1, caps_out_size),
             sizeof(struct armaccel_caps_v1));
  if (svc_call(base, job, ARMACCEL_SVC_SESSION, ARMACCEL_SVC_SESSION_OPEN, WORK_SESSION_REQ,
               sizeof(struct armaccel_session_open_v1), 0u, 0u, &r0, &r1) != 0 ||
      r0 != ARMACCEL_SVC_RES_OK) {
    return 0xBAD10001u;
  }
  session = r1;
  ok_count++;

  write_cstr(base, WORK_TITLE_STR, "ARMAccel ABI Smoke");
  be32_write(base, WORK_WINDOW_REQ + offsetof(struct armaccel_window_open_v1, session_handle), session);
  be32_write(base, WORK_WINDOW_REQ + offsetof(struct armaccel_window_open_v1, window_flags),
             ARMACCEL_WINDOW_FLAG_CLOSEGADGET | ARMACCEL_WINDOW_FLAG_DRAGBAR |
                 ARMACCEL_WINDOW_FLAG_DEPTHGADGET | ARMACCEL_WINDOW_FLAG_ACTIVATE);
  be32_write(base, WORK_WINDOW_REQ + offsetof(struct armaccel_window_open_v1, idcmp_mask), 0u);
  be32_write(base, WORK_WINDOW_REQ + offsetof(struct armaccel_window_open_v1, x), 40u);
  be32_write(base, WORK_WINDOW_REQ + offsetof(struct armaccel_window_open_v1, y), 40u);
  be32_write(base, WORK_WINDOW_REQ + offsetof(struct armaccel_window_open_v1, width), 420u);
  be32_write(base, WORK_WINDOW_REQ + offsetof(struct armaccel_window_open_v1, height), 120u);
  be32_write(base, WORK_WINDOW_REQ + offsetof(struct armaccel_window_open_v1, title_offset), WORK_TITLE_STR);
  be32_write(base, WORK_WINDOW_REQ + offsetof(struct armaccel_window_open_v1, title_size), 18u);
  if (svc_call(base, job, ARMACCEL_SVC_WINDOW, ARMACCEL_SVC_WINDOW_OPEN, WORK_WINDOW_REQ,
               sizeof(struct armaccel_window_open_v1), 0u, 0u, &r0, &r1) == 0 &&
      r0 == ARMACCEL_SVC_RES_OK) {
    window = r1;
    ok_count++;
  }

  /* INPUT_POLL baseline */
  if (svc_call(base, job, ARMACCEL_SVC_INPUT, ARMACCEL_SVC_INPUT_POLL, session, 0u, 0u, 0u, &r0,
               &r1) == 0 &&
      r0 == ARMACCEL_SVC_RES_OK) {
    ok_count++;
  }

  write_cstr(base, WORK_TEXT_STR, "ARMAccel service ABI smoke test");
  be32_write(base, WORK_REQ_REQ + offsetof(struct armaccel_requester_open_v1, session_handle), session);
  be32_write(base, WORK_REQ_REQ + offsetof(struct armaccel_requester_open_v1, window_handle), window);
  be32_write(base, WORK_REQ_REQ + offsetof(struct armaccel_requester_open_v1, requester_type),
             ARMACCEL_REQUESTER_MESSAGE);
  be32_write(base, WORK_REQ_REQ + offsetof(struct armaccel_requester_open_v1, text_offset), WORK_TEXT_STR);
  be32_write(base, WORK_REQ_REQ + offsetof(struct armaccel_requester_open_v1, text_size), 31u);
  if (svc_call(base, job, ARMACCEL_SVC_REQUESTER, ARMACCEL_SVC_REQUESTER_OPEN, WORK_REQ_REQ,
               sizeof(struct armaccel_requester_open_v1), 0u, 0u, &r0, &r1) == 0 &&
      r0 == ARMACCEL_SVC_RES_OK) {
    ok_count++;
  }

  write_cstr(base, WORK_PATH_STR, "S:Startup-Sequence");
  be32_write(base, WORK_FILE_REQ + offsetof(struct armaccel_file_open_v1, session_handle), session);
  be32_write(base, WORK_FILE_REQ + offsetof(struct armaccel_file_open_v1, open_flags), 0u);
  be32_write(base, WORK_FILE_REQ + offsetof(struct armaccel_file_open_v1, path_offset), WORK_PATH_STR);
  be32_write(base, WORK_FILE_REQ + offsetof(struct armaccel_file_open_v1, path_size), 18u);
  if (svc_call(base, job, ARMACCEL_SVC_FILE, ARMACCEL_SVC_FILE_OPEN, WORK_FILE_REQ,
               sizeof(struct armaccel_file_open_v1), 0u, 0u, &r0, &r1) == 0 &&
      r0 == ARMACCEL_SVC_RES_OK) {
    file = r1;
    ok_count++;

    be32_write(base, WORK_FILE_IO_REQ + offsetof(struct armaccel_file_io_v1, session_handle), session);
    be32_write(base, WORK_FILE_IO_REQ + offsetof(struct armaccel_file_io_v1, file_handle), file);
    be32_write(base, WORK_FILE_IO_REQ + offsetof(struct armaccel_file_io_v1, buffer_offset), WORK_FILE_BUF);
    be32_write(base, WORK_FILE_IO_REQ + offsetof(struct armaccel_file_io_v1, buffer_size), 128u);
    be32_write(base, WORK_FILE_IO_REQ + offsetof(struct armaccel_file_io_v1, file_off_hi), 0u);
    be32_write(base, WORK_FILE_IO_REQ + offsetof(struct armaccel_file_io_v1, file_off_lo), 0u);

    if (svc_call(base, job, ARMACCEL_SVC_FILE, ARMACCEL_SVC_FILE_READ, WORK_FILE_IO_REQ,
                 sizeof(struct armaccel_file_io_v1), 0u, 0u, &r0, &r1) == 0 &&
        r0 == ARMACCEL_SVC_RES_OK) {
      ok_count++;
    }
    (void)svc_call(base, job, ARMACCEL_SVC_FILE, ARMACCEL_SVC_FILE_CLOSE, file, 0u, 0u, 0u, &r0,
                   &r1);
  }

  if (svc_call(base, job, ARMACCEL_SVC_TIMER, ARMACCEL_SVC_TIMER_GET_TICK, 0u, 0u, 0u, 0u, &r0,
               &r1) == 0 &&
      r0 == ARMACCEL_SVC_RES_OK) {
    ok_count++;
  }
  (void)svc_call(base, job, ARMACCEL_SVC_TIMER, ARMACCEL_SVC_TIMER_SLEEP_MS, 50u, 0u, 0u, 0u, &r0,
                 &r1);

  if (window != 0u) {
    (void)svc_call(base, job, ARMACCEL_SVC_WINDOW, ARMACCEL_SVC_WINDOW_CLOSE, window, 0u, 0u, 0u,
                   &r0, &r1);
  }
  if (session != 0u) {
    (void)svc_call(base, job, ARMACCEL_SVC_SESSION, ARMACCEL_SVC_SESSION_CLOSE, session, 0u, 0u, 0u,
                   &r0, &r1);
  }

  return 0xA5100000u | (uint64_t)(ok_count & 0xFFFFu);
}
