// SPDX-License-Identifier: MIT
#ifndef ARMACCEL_SERVICE_ABI_H
#define ARMACCEL_SERVICE_ABI_H

#include <stdint.h>
#include "armaccel_abi_types_v1.h"

/*
 * ARMAccel Service ABI v1
 *
 * Canonical model:
 * - App artifact is AArch64 payload.
 * - Payload instructions execute on coprocessor side.
 * - AmigaOS services are owned by Amiga side and are proxied via this ABI.
 *
 * Wire endian:
 * - Service frame fields are big-endian 32-bit values.
 * - Signed values are two's-complement encoded in BE32 fields.
 * - Structured blobs explicitly declare endianness in descriptors.
 */

#define ARMACCEL_SVC_ABI_MAJOR 1u
#define ARMACCEL_SVC_ABI_MINOR 0u

#define ARMACCEL_SVC_FRAME_MAGIC 0x41535643u /* "ASVC" */
#define ARMACCEL_SVC_FRAME_VERSION 1u

/* Shared call frame location within AJOB extension bytes. */
#define ARMACCEL_SVC_FRAME_JOBDESC_OFF 0xC0u
#define ARMACCEL_SVC_FRAME_SIZE 0x40u

/* Frame layout offsets from ARMACCEL_SVC_FRAME_JOBDESC_OFF. */
#define ARMACCEL_SVC_OFF_MAGIC      0x00u
#define ARMACCEL_SVC_OFF_VERSION    0x04u
#define ARMACCEL_SVC_OFF_SEQ        0x08u
#define ARMACCEL_SVC_OFF_STATE      0x0Cu
#define ARMACCEL_SVC_OFF_SERVICE    0x10u
#define ARMACCEL_SVC_OFF_OPCODE     0x14u
#define ARMACCEL_SVC_OFF_FLAGS      0x18u
#define ARMACCEL_SVC_OFF_ARG0       0x1Cu
#define ARMACCEL_SVC_OFF_ARG1       0x20u
#define ARMACCEL_SVC_OFF_ARG2       0x24u
#define ARMACCEL_SVC_OFF_ARG3       0x28u
#define ARMACCEL_SVC_OFF_RESULT0    0x2Cu
#define ARMACCEL_SVC_OFF_RESULT1    0x30u
#define ARMACCEL_SVC_OFF_EVENTCLASS 0x34u
#define ARMACCEL_SVC_OFF_EVENTCODE  0x38u
#define ARMACCEL_SVC_OFF_RESERVED   0x3Cu

/* Frame states. */
#define ARMACCEL_SVC_STATE_IDLE     0u
#define ARMACCEL_SVC_STATE_PENDING  1u
#define ARMACCEL_SVC_STATE_DONE     2u
#define ARMACCEL_SVC_STATE_ERROR    3u
#define ARMACCEL_SVC_STATE_ASYNC    4u

/*
 * Async/in-flight rule for v1:
 * - one in-flight service request per session
 * - completion is signaled by STATE_DONE/STATE_ERROR or completion event
 */
#define ARMACCEL_SVC_MAX_INFLIGHT_PER_SESSION 1u

/* Generic call flags. */
#define ARMACCEL_SVC_FLAG_ASYNC     0x00000001u
#define ARMACCEL_SVC_FLAG_NONBLOCK  0x00000002u

/* Generic blob flags. */
#define ARMACCEL_BLOB_FLAG_WRITABLE      0x00000001u
#define ARMACCEL_BLOB_FLAG_NUL_TERM      0x00000002u
#define ARMACCEL_BLOB_FLAG_PERSISTENT    0x00000004u
#define ARMACCEL_BLOB_FLAG_RUNTIME_OWNED 0x00000008u

/* Blob endianness declarations. */
#define ARMACCEL_BLOB_ENDIAN_NONE 0u
#define ARMACCEL_BLOB_ENDIAN_LE32 1u
#define ARMACCEL_BLOB_ENDIAN_BE32 2u

/* Blob string encodings. */
#define ARMACCEL_STR_ENC_NONE     0u
#define ARMACCEL_STR_ENC_ASCII    1u
#define ARMACCEL_STR_ENC_UTF8     2u
#define ARMACCEL_STR_ENC_AMIGA8   3u

/* Generic handle model. */
typedef uint32_t armaccel_svc_handle_t;
#define ARMACCEL_HANDLE_INVALID 0u

/* Handle classes for diagnostics/event typing. */
#define ARMACCEL_HANDLE_CLASS_SESSION   1u
#define ARMACCEL_HANDLE_CLASS_WINDOW    2u
#define ARMACCEL_HANDLE_CLASS_MENU      3u
#define ARMACCEL_HANDLE_CLASS_REQUESTER 4u
#define ARMACCEL_HANDLE_CLASS_SURFACE   5u
#define ARMACCEL_HANDLE_CLASS_FILE      6u
#define ARMACCEL_HANDLE_CLASS_TIMER     7u
#define ARMACCEL_HANDLE_CLASS_DEVICE    8u
#define ARMACCEL_HANDLE_CLASS_DATATYPE  9u

/*
 * Handle lifetime rules:
 * - Handles are scoped to a session.
 * - Closing a session invalidates all child handles.
 * - 0 is always invalid.
 * - Runtime may recycle handle values after close.
 */

/* Service classes. */
#define ARMACCEL_SVC_SESSION      1u
#define ARMACCEL_SVC_WINDOW       2u
#define ARMACCEL_SVC_MENU         3u
#define ARMACCEL_SVC_REQUESTER    4u
#define ARMACCEL_SVC_SURFACE      5u
#define ARMACCEL_SVC_INPUT        6u
#define ARMACCEL_SVC_FILE         7u
#define ARMACCEL_SVC_TIMER        8u
#define ARMACCEL_SVC_CLIPBOARD    9u
#define ARMACCEL_SVC_AUDIO        10u
#define ARMACCEL_SVC_VIDEO        11u
#define ARMACCEL_SVC_DEVICE_IO    12u
#define ARMACCEL_SVC_BLOCK_IO     13u
#define ARMACCEL_SVC_NET_IO       14u
#define ARMACCEL_SVC_DATATYPE     15u

/* Service capability bits for NOTE/QueryELF required_services. */
#define ARMACCEL_CAP_WINDOW       0x00000001u
#define ARMACCEL_CAP_MENU         0x00000002u
#define ARMACCEL_CAP_REQUESTER    0x00000004u
#define ARMACCEL_CAP_SURFACE      0x00000008u
#define ARMACCEL_CAP_INPUT        0x00000010u
#define ARMACCEL_CAP_FILE         0x00000020u
#define ARMACCEL_CAP_TIMER        0x00000040u
#define ARMACCEL_CAP_CLIPBOARD    0x00000080u
#define ARMACCEL_CAP_AUDIO        0x00000100u
#define ARMACCEL_CAP_VIDEO        0x00000200u
#define ARMACCEL_CAP_DEVICE_IO    0x00000400u
#define ARMACCEL_CAP_BLOCK_IO     0x00000800u
#define ARMACCEL_CAP_NET_IO       0x00001000u
#define ARMACCEL_CAP_DATATYPE     0x00002000u

/* Profile capability bits. */
#define ARMACCEL_PROFILE_CORE     0x00000001u
#define ARMACCEL_PROFILE_APP       0x00000002u
#define ARMACCEL_PROFILE_RENDER    0x00000004u
#define ARMACCEL_PROFILE_BATCH     0x00000008u
#define ARMACCEL_PROFILE_GUEST     0x00000010u

/* Runtime feature flags. */
#define ARMACCEL_FEAT_EVENT_RING       0x00000001u
#define ARMACCEL_FEAT_ASYNC_CALLS      0x00000002u
#define ARMACCEL_FEAT_MULTI_WINDOW     0x00000004u
#define ARMACCEL_FEAT_PARTIAL_PRESENT  0x00000008u
#define ARMACCEL_FEAT_RAW_DEVICE_IO    0x00000010u
#define ARMACCEL_FEAT_EVENT_TIMESTAMP  0x00000020u

/* Generic event classes. */
#define ARMACCEL_EVT_COMPLETION    1u
#define ARMACCEL_EVT_WINDOW        2u
#define ARMACCEL_EVT_INPUT         3u
#define ARMACCEL_EVT_MENU          4u
#define ARMACCEL_EVT_REQUESTER     5u
#define ARMACCEL_EVT_TIMER         6u
#define ARMACCEL_EVT_FILE          7u
#define ARMACCEL_EVT_DEVICE        8u

/* Generic event codes for EVT_WINDOW. */
#define ARMACCEL_EVT_WINDOW_CLOSE   1u
#define ARMACCEL_EVT_WINDOW_RESIZE  2u
#define ARMACCEL_EVT_WINDOW_REFRESH 3u

/* Generic event codes for EVT_INPUT. */
#define ARMACCEL_EVT_INPUT_KEYDOWN   1u
#define ARMACCEL_EVT_INPUT_KEYUP     2u
#define ARMACCEL_EVT_INPUT_MOUSEMOVE 3u
#define ARMACCEL_EVT_INPUT_BUTTON    4u
#define ARMACCEL_EVT_INPUT_WHEEL     5u

/* SESSION opcodes. */
#define ARMACCEL_SVC_SESSION_OPEN   1u
#define ARMACCEL_SVC_SESSION_CLOSE  2u
#define ARMACCEL_SVC_SESSION_YIELD  3u
#define ARMACCEL_SVC_SESSION_GET_CAPS 4u

/* WINDOW opcodes. */
#define ARMACCEL_SVC_WINDOW_OPEN       1u
#define ARMACCEL_SVC_WINDOW_CLOSE      2u
#define ARMACCEL_SVC_WINDOW_SET_TITLE  3u
#define ARMACCEL_SVC_WINDOW_GET_BOUNDS 4u
#define ARMACCEL_SVC_WINDOW_SET_BOUNDS 5u

/* MENU opcodes. */
#define ARMACCEL_SVC_MENU_SET       1u
#define ARMACCEL_SVC_MENU_CLEAR     2u
#define ARMACCEL_SVC_MENU_POLL      3u

/* REQUESTER opcodes. */
#define ARMACCEL_SVC_REQUESTER_OPEN 1u
#define ARMACCEL_SVC_REQUESTER_POLL 2u

/* SURFACE opcodes. */
#define ARMACCEL_SVC_SURFACE_ALLOC   1u
#define ARMACCEL_SVC_SURFACE_PRESENT 2u
#define ARMACCEL_SVC_SURFACE_FREE    3u

/* INPUT opcodes. */
#define ARMACCEL_SVC_INPUT_POLL     1u
#define ARMACCEL_SVC_INPUT_WAIT     2u

/* FILE opcodes. */
#define ARMACCEL_SVC_FILE_OPEN      1u
#define ARMACCEL_SVC_FILE_READ      2u
#define ARMACCEL_SVC_FILE_WRITE     3u
#define ARMACCEL_SVC_FILE_SEEK      4u
#define ARMACCEL_SVC_FILE_CLOSE     5u
#define ARMACCEL_SVC_FILE_LISTDIR   6u
#define ARMACCEL_SVC_FILE_STAT      7u
#define ARMACCEL_SVC_FILE_RENAME    8u
#define ARMACCEL_SVC_FILE_DELETE    9u
#define ARMACCEL_SVC_FILE_MKDIR     10u
#define ARMACCEL_SVC_FILE_RMDIR     11u

/* TIMER opcodes. */
#define ARMACCEL_SVC_TIMER_SLEEP_MS 1u
#define ARMACCEL_SVC_TIMER_GET_TICK 2u

/* AUDIO/VIDEO/DEVICE raw opcodes (v1 reserved seed). */
#define ARMACCEL_SVC_AUDIO_OPEN     1u
#define ARMACCEL_SVC_AUDIO_WRITE    2u
#define ARMACCEL_SVC_AUDIO_CLOSE    3u
#define ARMACCEL_SVC_VIDEO_STREAM_OPEN 1u
#define ARMACCEL_SVC_VIDEO_STREAM_PUSH 2u
#define ARMACCEL_SVC_VIDEO_STREAM_CLOSE 3u
#define ARMACCEL_SVC_DEVICE_IO_OPEN 1u
#define ARMACCEL_SVC_DEVICE_IO_DOIO 2u
#define ARMACCEL_SVC_DEVICE_IO_CLOSE 3u

/* DATATYPE opcodes (v1 seeded). */
#define ARMACCEL_SVC_DATATYPE_IDENTIFY 1u
#define ARMACCEL_SVC_DATATYPE_LOAD     2u
#define ARMACCEL_SVC_DATATYPE_CONVERT  3u
#define ARMACCEL_SVC_DATATYPE_METADATA 4u
#define ARMACCEL_SVC_DATATYPE_SAVE     5u

/* Pixel formats for surfaces/events/caps bitmasks. */
#define ARMACCEL_PIXFMT_CLUT8      1u
#define ARMACCEL_PIXFMT_R5G6B5     2u
#define ARMACCEL_PIXFMT_R8G8B8     3u
#define ARMACCEL_PIXFMT_B8G8R8     4u
#define ARMACCEL_PIXFMT_A8R8G8B8   5u
#define ARMACCEL_PIXFMT_A8B8G8R8   6u
#define ARMACCEL_PIXFMT_R8G8B8A8   7u
#define ARMACCEL_PIXFMT_B8G8R8A8   8u

/* Window flags. */
#define ARMACCEL_WINDOW_FLAG_CLOSEGADGET 0x00000001u
#define ARMACCEL_WINDOW_FLAG_DRAGBAR     0x00000002u
#define ARMACCEL_WINDOW_FLAG_DEPTHGADGET 0x00000004u
#define ARMACCEL_WINDOW_FLAG_SIZEGADGET  0x00000008u
#define ARMACCEL_WINDOW_FLAG_ACTIVATE    0x00000010u

/* Requester types. */
#define ARMACCEL_REQUESTER_MESSAGE   1u
#define ARMACCEL_REQUESTER_YESNOCANCEL 2u
#define ARMACCEL_REQUESTER_STRING    3u
#define ARMACCEL_REQUESTER_FILE      4u

/* Generic status/result codes (result0). */
#define ARMACCEL_SVC_RES_OK            0u
#define ARMACCEL_SVC_RES_UNSUPPORTED   1u
#define ARMACCEL_SVC_RES_INVALID_ARG   2u
#define ARMACCEL_SVC_RES_BAD_HANDLE    3u
#define ARMACCEL_SVC_RES_RANGE         4u
#define ARMACCEL_SVC_RES_IO            5u
#define ARMACCEL_SVC_RES_BUSY          6u
#define ARMACCEL_SVC_RES_TIMEOUT       7u
#define ARMACCEL_SVC_RES_INTERNAL      8u
#define ARMACCEL_SVC_RES_PERMISSION    9u

/* Error namespace encoding for result1: [31:24 namespace] [23:0 code]. */
#define ARMACCEL_SVC_ERRNS_GENERIC 0u
#define ARMACCEL_SVC_ERRNS_DOS     1u
#define ARMACCEL_SVC_ERRNS_INTUITION 2u
#define ARMACCEL_SVC_ERRNS_EXEC    3u
#define ARMACCEL_SVC_ERRNS_DEVICE  4u
#define ARMACCEL_SVC_ERR_PACK(ns, code) ((((ns) & 0xFFu) << 24u) | ((code) & 0x00FFFFFFu))

/*
 * Foundational wire structs (all u32 fields are BE32 on wire).
 * Payload/runtime structs in local memory may be native-endian equivalents,
 * but conversion at wire boundaries is mandatory.
 */

struct armaccel_svc_frame_v1 {
  uint32_t magic;
  uint32_t version;
  uint32_t seq;
  uint32_t state;
  uint32_t service;
  uint32_t opcode;
  uint32_t flags;
  uint32_t arg0;
  uint32_t arg1;
  uint32_t arg2;
  uint32_t arg3;
  uint32_t result0;
  uint32_t result1;
  uint32_t event_class;
  uint32_t event_code;
  uint32_t reserved;
};

/* Generic descriptor for structured input/output blobs in shared memory. */
struct armaccel_blob_desc_v1 {
  uint32_t offset;
  uint32_t size;
  uint32_t capacity;
  uint32_t flags;
  uint32_t format;
  uint32_t endian;
  uint32_t str_encoding;
  uint32_t reserved0;
};

/* Event ring descriptor in shared memory. */
struct armaccel_event_ring_v1 {
  uint32_t offset;
  uint32_t capacity;
  uint32_t event_size;
  uint32_t head;
  uint32_t tail;
  uint32_t flags;
  uint32_t overflow_count;
  uint32_t reserved0;
};

/* Generic event record. */
struct armaccel_event_v1 {
  uint32_t seq;
  uint32_t session_handle;
  uint32_t event_class;
  uint32_t event_code;
  uint32_t object_handle;
  uint32_t flags;
  uint32_t data0;
  uint32_t data1;
  uint32_t data2;
  uint32_t data3;
  uint32_t tick_hi;
  uint32_t tick_lo;
};

/* Capability record returned by SESSION_GET_CAPS/SESSION_OPEN. */
struct armaccel_caps_v1 {
  uint32_t abi_major;
  uint32_t abi_minor;
  uint32_t service_caps;
  uint32_t profile_caps;
  uint32_t feature_flags;
  uint32_t coord_format;
  uint32_t size_format;
  uint32_t time_unit_sleep;
  uint32_t time_unit_event;
  uint32_t pixel_format_caps;
  uint32_t max_inflight_per_session;
  uint32_t max_windows;
  uint32_t max_surfaces;
  uint32_t max_files;
  uint32_t max_blob_bytes;
  uint32_t max_event_ring_events;
  uint32_t tick_hz_hi;
  uint32_t tick_hz_lo;
  uint32_t reserved0;
  uint32_t reserved1;
};

/* SESSION_OPEN request blob. Returns session handle in frame.result1 on success. */
struct armaccel_session_open_v1 {
  uint32_t requested_service_caps;
  uint32_t requested_profile_caps;
  uint32_t event_ring_desc_offset;
  uint32_t event_ring_desc_size;
  uint32_t caps_out_offset;
  uint32_t caps_out_size;
  uint32_t flags;
  uint32_t reserved0;
};

/*
 * WINDOW_OPEN request blob. Returns window handle in frame.result1 on success.
 * x/y are signed s32 pixels; width/height/min/max are u32 pixels.
 */
struct armaccel_window_open_v1 {
  uint32_t session_handle;
  uint32_t window_flags;
  uint32_t idcmp_mask;
  uint32_t x;
  uint32_t y;
  uint32_t width;
  uint32_t height;
  uint32_t min_width;
  uint32_t min_height;
  uint32_t max_width;
  uint32_t max_height;
  uint32_t title_offset;
  uint32_t title_size;
  uint32_t screen_name_offset;
  uint32_t screen_name_size;
  uint32_t reserved0;
};

/*
 * SURFACE_ALLOC request blob. Returns surface handle in frame.result1 on success.
 * width/height are u32 pixels, stride is u32 bytes.
 */
struct armaccel_surface_desc_v1 {
  uint32_t session_handle;
  uint32_t attach_window_handle;
  uint32_t pixel_format;
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  uint32_t buffer_offset;
  uint32_t buffer_size;
  uint32_t flags;
  uint32_t reserved0;
};

/* FILE_OPEN request blob. Returns file handle in frame.result1 on success. */
struct armaccel_file_open_v1 {
  uint32_t session_handle;
  uint32_t open_flags;
  uint32_t path_offset;
  uint32_t path_size;
  uint32_t create_perms;
  uint32_t reserved0;
};

/* FILE_READ/WRITE/SEEK request blob (64-bit file offset split hi/lo). */
struct armaccel_file_io_v1 {
  uint32_t session_handle;
  uint32_t file_handle;
  uint32_t buffer_offset;
  uint32_t buffer_size;
  uint32_t file_off_hi;
  uint32_t file_off_lo;
  uint32_t io_flags;
  uint32_t reserved0;
};

/* MENU_SET request blob. */
struct armaccel_menu_set_v1 {
  uint32_t session_handle;
  uint32_t window_handle;
  uint32_t menu_blob_offset;
  uint32_t menu_blob_size;
  uint32_t menu_format;
  uint32_t flags;
  uint32_t reserved0;
  uint32_t reserved1;
};

/* REQUESTER_OPEN request blob. */
struct armaccel_requester_open_v1 {
  uint32_t session_handle;
  uint32_t window_handle;
  uint32_t requester_type;
  uint32_t requester_flags;
  uint32_t text_offset;
  uint32_t text_size;
  uint32_t options_offset;
  uint32_t options_size;
  uint32_t result_offset;
  uint32_t result_capacity;
  uint32_t reserved0;
  uint32_t reserved1;
};

/* DATATYPE request blob (v1 seeded). */
struct armaccel_datatype_request_v1 {
  uint32_t session_handle;
  uint32_t source_blob_offset;
  uint32_t source_blob_size;
  uint32_t source_kind;
  uint32_t target_format;
  uint32_t options_blob_offset;
  uint32_t options_blob_size;
  uint32_t output_blob_offset;
  uint32_t output_blob_capacity;
  uint32_t metadata_blob_offset;
  uint32_t metadata_blob_capacity;
  uint32_t flags;
};

#endif
