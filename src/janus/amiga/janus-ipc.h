#ifndef JANUS_IPC_H
#define JANUS_IPC_H

#include <stdint.h>
#include <stdbool.h>

// Include Amiga headers if compiling for Amiga
#ifdef __amigaos__
#include <exec/types.h>
#include <exec/nodes.h>
#include <exec/ports.h>
#include <exec/lists.h>
#include <exec/messages.h>
#endif

// Define placeholder types for non-Amiga platforms if not already defined
#ifndef __amigaos__
typedef void* APTR;
typedef unsigned char UBYTE;
typedef unsigned short UWORD;
typedef unsigned long ULONG;
#endif

// Wire protocol constants for Janus IPC.

#define JANUS_CMD_FRACTAL 0x0001
#define JANUS_CMD_TEXT 0x0002

#define JANUS_MSG_HEADER_LEN 4

#define JANUS_FRACTAL_PAYLOAD_LEN 32
#define JANUS_FRACTAL_MSG_LEN (JANUS_MSG_HEADER_LEN + JANUS_FRACTAL_PAYLOAD_LEN)

// Fractal message layout (big-endian, 16.16 fixed-point for cx/cy/scale):
// cmd, len, width, height, stride, max_iter, dst_ptr, status_ptr, cx, cy, scale

#define JANUS_OFF_CMD 0
#define JANUS_OFF_LEN 2

#define JANUS_OFF_WIDTH 4
#define JANUS_OFF_HEIGHT 6
#define JANUS_OFF_STRIDE 8
#define JANUS_OFF_MAX_ITER 10
#define JANUS_OFF_DST_PTR 12
#define JANUS_OFF_STATUS_PTR 16
#define JANUS_OFF_CX 20
#define JANUS_OFF_CY 24
#define JANUS_OFF_SCALE 28

// Define callback type for message handling
typedef void (*janus_msg_callback_t)(uint16_t cmd, const uint8_t* payload, uint16_t payload_len);

// Public API functions for Janus IPC
bool janus_ipc_init(uintptr_t base_addr, uint16_t size, uint16_t flags);
uint16_t janus_send_message(uint16_t cmd, const uint8_t* payload, uint16_t payload_len);
void janus_process_messages(void);
bool janus_get_status(void);
uint16_t janus_get_free_space(void);
uint16_t janus_send_text(const char* text);
uint16_t janus_send_fractal(uint16_t width, uint16_t height, uint16_t stride,
                           uint16_t max_iter, uintptr_t dst_ptr, uintptr_t status_ptr,
                           int32_t cx, int32_t cy, uint32_t scale);

// Enhanced Amiga-style message handling functions
#ifdef __amigaos__
struct MsgPort *janus_create_msg_port(const char *name, int16_t pri);
#endif

// Send a message to a remote processor (could be on Pi, network, etc.)
uint16_t janus_send_remote_message(uint16_t cmd, const uint8_t* payload, uint16_t payload_len,
                                   uintptr_t remote_addr);

// Register a callback for when messages arrive
void janus_set_message_callback(janus_msg_callback_t callback);

#endif