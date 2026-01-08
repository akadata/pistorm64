# A314-Based Z2/Z3 Device Guide

Purpose: describe how to extend the existing A314 transport to implement a new Pi-side Z2/Z3 device and a matching Amiga-side client/driver. This uses the existing A314 daemon + Amiga device as the transport layer and avoids chip RAM traffic.

## High-Level Architecture

- **Pi-side daemon**: `a314/a314.cc` multiplexes services and moves packets between the Amiga and Pi through the A314 comms area.
- **Amiga-side device**: `a314/a314device/*` exposes `a314.device` to AmigaOS. Clients open the device and send commands.
- **Services**: userland processes on the Pi register a service name, then accept connections from Amiga-side clients.
- **Transport**: bidirectional packet queues (A2R and R2A) live in a small shared comms area with head/tail pointers.

This is a good blueprint for a new Pi-side Z2/Z3 service because:
- The daemon already supports **read/write memory** (mapped Pi memory or bus reads).
- It already supports **multiple services** and **stream multiplexing**.
- Amiga-side client code exists and can be adapted.

## Key Code Pointers (Pi-side)

- **Daemon**: `a314/a314.cc`
  - Message types: `MSG_*` (register, connect, data, read/write mem).
  - Service registry and on-demand launch.
  - `handle_msg_read_mem_req()` / `handle_msg_write_mem_req()` for map-backed memory IO.
- **Config file**: `a314/files_pi/a314d.conf`
  - Service name → process mapping (Python services included).
- **Python service example**: `a314/files_pi/picmd.py`
  - Shows message framing and request/response pattern.

## Key Code Pointers (Amiga-side)

- **Device**: `a314/a314device/*`
  - `a314device/protocol.h` (packet protocol, comms area).
  - `a314device/device.c` (device init/open/IO).
  - `a314device/a314driver.c` (queue handling, connect/data/eos/reset).
- **Client examples**:
  - `a314/software-amiga/pi_pistorm/pi.c` (connect/write/read flow).
  - `a314/software-amiga/a314fs_pistorm/a314fs.c` (file service usage).
  - `a314/software-amiga/piaudio_pistorm/piaudio.c` (streaming buffers).
- **Autoconf patterns**:
  - Z2/Z3 mapping: `platforms/amiga/amiga-autoconf.c`
  - PiSCSI example: `platforms/amiga/piscsi/*`
  - Pi-Net example: `platforms/amiga/net/*`

## Transport Summary

### Packet Types (from `a314/a314device/protocol.h`)
- `PKT_CONNECT`, `PKT_DATA`, `PKT_EOS`, `PKT_RESET` for logical channels.
- R2A/A2R head/tail indexes maintained in the comms area.

### Messages (from `a314/a314.cc`)
- `MSG_REGISTER_REQ/RES`: register a Pi service name.
- `MSG_CONNECT/RESPONSE`: connect Amiga → Pi service.
- `MSG_DATA`: payload data.
- `MSG_READ_MEM_REQ/RES`, `MSG_WRITE_MEM_REQ/RES`: memory IO.

## How To Build a New Z2/Z3 Device

### 1) Decide the memory model

Pick one:
- **Z2**: 24-bit address space (≤ 8 MB typical). Good for compatibility.
- **Z3**: 32-bit address space. Better for bulk buffers and capture.

Use `platforms/amiga/amiga-autoconf.c` as a template:
- Map Z2 or Z3 region
- Register a device ID and Autoconf attributes
- Provide a “comms area” window in Z2/Z3 RAM (if needed)

### 2) Define the device contract

Example for capture device:
- A fixed Z2/Z3 buffer (framebuffer) mapped by emulator.
- A small control block (ring buffer or mailbox) that Amiga writes to:
  - frame width/height/planes
  - pointer to screen buffer
  - sequence counter / flags

### 3) Implement Pi-side service

Option A: extend A314 daemon with a new built-in service.
- Use the message types already implemented.
- Read/write Amiga memory using `MSG_READ_MEM_REQ` and `MSG_WRITE_MEM_REQ`.

Option B: add a new userland service (Python or C) like `picmd.py`.
- Register service name via `MSG_REGISTER_REQ`.
- On `MSG_CONNECT`, begin streaming or request memory.

### 4) Implement Amiga-side client/driver

Use one of these patterns:
- **Device driver** (Z2/Z3): best for background capture or streaming.
- **Commodity**: foreground but simple; writes to Z2/Z3 buffer.

Reference:
- `a314/software-amiga/pi_pistorm/pi.c` shows the full connect/read/write loop.
- `a314/software-amiga/piaudio_pistorm/piaudio.c` shows continuous streaming.

## Example Flow: “Pi Capture Service”

1) Amiga app:
   - Opens `a314.device`
   - `A314_CONNECT` to `pistorm_capture`
   - Sends `CAPTURE_START` with width/height/planes and buffer address
2) Pi service:
   - Receives start message
   - Uses `MSG_READ_MEM_REQ` to read bitplanes or Z3 framebuffer
   - Sends back a status or streams to disk/RTG window

## How PiSCSI / Pi-Net Inform This

PiSCSI (`platforms/amiga/piscsi/*`) shows:
- How to register an Autoconf device
- How to use Z2 memory as a window
- How to split IO into command/data buffers

Pi-Net (`platforms/amiga/net/*`) shows:
- Simple register window for status/control
- Typical driver init/IO patterns on Amiga

## Notes for Screen Capture

For reliable capture:
- Prefer Z3 or RTG memory over chip RAM
- Use A314 to command “copy screen to Z3 buffer”
- Then Pi reads the Z3 buffer directly (no bus contention)

## Next Steps (Suggested)

- Define a tiny “capture control block” in Z3 RAM.
- Add a Pi service `pistorm_cap` to `a314d.conf`.
- Prototype a minimal Amiga client that writes the control block and triggers capture.

If you want, we can draft:
- A concrete capture control block structure
- A minimal Amiga client (C) using `a314.device`
- A matching Pi service stub
