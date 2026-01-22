#ifndef JANUS_IPC_H
#define JANUS_IPC_H

// Wire protocol constants for Janus IPC.

#define JANUS_CMD_FRACTAL 0x0001
#define JANUS_CMD_TEXT 0x0002

#define JANUS_MSG_HEADER_LEN 4

#define JANUS_FRACTAL_PAYLOAD_LEN 28
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

#endif
