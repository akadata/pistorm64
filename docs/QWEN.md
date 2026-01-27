# QWEN.md — JANUS IPC & Co‑Processor Contract

This document explains what the JANUS bus is in the context of Kernel PiStorm64, how the IPC ring is intended to work, and how to write code that targets it from both Amiga and host (Linux/Windows) sides. The purpose is to give Qwen (and any other AI assistant) a clear, stable contract to follow when generating or modifying code in this project.

---

## 1. What JANUS is (in this project)

Historically, Commodore’s Janus system linked Amiga and PC Bridgeboards using shared RAM and a software layer (janus.library) to provide inter‑process communication (IPC). The PC lived on a Zorro card; the Amiga acted as host and UI, and the two sides shared:

* Dual‑ported RAM (mailboxes + data buffers)
* Doorbell/interrupt registers
* Handlers that moved keyboard, mouse, video and files between the two systems

In Kernel PiStorm64, **JANUS** is the name for our **co‑processor IPC fabric**:

* The Amiga runs as usual (Workbench, Intuition, AmigaOS APIs).
* The Pi (and beyond it, other machines like an i9 host, RTX GPU, or QEMU guest) acts as a **remote co‑processor**.
* Communication happens by **small commands over a ring buffer** and **data in shared memory**, not by streaming pixels or bulk data over the control plane.

The goal is to treat the Pi (or a remote machine behind it) as a disciplined hardware companion, not a random firehose. The ring is the “control plane”; shared RAM, sockets, and files are the “data plane.”

---

## 2. Design principles

Qwen should respect these principles whenever touching JANUS‑related code:

1. **Ring is control, RAM is data**
   The JANUS ring is only for small messages: commands, descriptors, statuses. Bulk data (pixels, audio buffers, large structs) must live in shared memory regions that are mapped into Amiga address space and accessible to the Pi.

2. **IPC is transport‑agnostic**
   The contract should work regardless of whether the remote side is local on the Pi, on `homer` over TCP, or even tunneled via some other link. The core abstractions must not depend on a specific transport.

3. **C code must be portable**
   Code should compile cleanly with both **GCC** and **Clang**, on:

   * Amiga (m68k‑amigaos, using the AmigaOS SDK)
   * Linux (Arch, Alpine, etc.)
   * Potentially Windows (via MinGW or similar) for host tools

4. **Endianness and alignment are explicit**
   The Amiga is big‑endian 68k. Many modern hosts (x86_64, ARMv8) are little‑endian. All on‑wire/in‑ring layouts must use explicit big‑endian fields, with clear helpers for reading/writing.

5. **Security is pluggable and future‑proof**
   JANUS must be able to authenticate requests when talking to remote hosts. For now, the simplest model is a **numeric shared secret** in the protocol header. That can later be replaced or augmented with stronger crypto, without rewriting every client.

6. **No Amiga‑side emulation of remote CPUs**
   The Amiga sees co‑processors via JANUS as **services**, not as “fake 68k code.” Qwen must not try to emulate arbitrary foreign ABIs on the Amiga side.

---

## 3. The JANUS IPC ring

The JANUS ring is a small, circular buffer in Amiga address space that both the emulated 68k and the Pi can access.

Key points:

* Ring size is small (currently 4 KB). This is fine because it only carries **control messages**.
* Each message has:

  * A fixed‑format header (command ID + length) in big‑endian.
  * A payload whose layout depends on the command.
* Amiga writes messages into the ring and rings a **doorbell** (e.g., via a debug register or a dedicated register write) to notify the Pi.
* The Pi drains the ring, processes commands, writes results into shared memory, and optionally sets status flags or sends reply messages.

The ring is **not** a data stream for frames, textures, or large files. Use it like a message queue for job descriptors.

---

## 4. Doorbells and status

The doorbell mechanism is how the Amiga side tells the Pi side, “there is work pending in the ring.”

Conceptually:

* Amiga:

  * Writes one or more messages into the ring.
  * Updates the write index.
  * Writes to a specific register (doorbell) to signal “new work.”

* Pi:

  * Sees the doorbell write in the emulator/platform handler.
  * Reads from the ring until all pending messages are consumed.
  * For each message:

    * Acts on it.
    * Writes back status (often a single word in shared memory) and/or result data in the shared buffer.

Status words:

* Typically reside in Amiga‑visible RAM.
* Are pointed to by fields inside the command payload.
* Are polled or waited on by the Amiga program (e.g., `while (*status == 0) Delay(1);`).

Qwen should:

* Keep doorbell handling **in one place** (e.g., the pistorm‑dev or janus host module), not scattered.
* Use small, explicit enums or constants for status values (0 = idle/pending, 1 = success, other values for errors).

---

## 5. Coding guidelines — Amiga side (m68k)

Amiga‑side JANUS code lives under:

* `src/janus/amiga/`

General rules:

1. **Use the AmigaOS APIs properly**

   * Open libraries with `OpenLibrary()` and close with `CloseLibrary()`.
   * Use Intuition for windows and gadgets, Graphics for drawing, Exec for memory and ports.
   * Respect the patterns documented in the Hardware and Libraries manuals (already present under `Hardware/ADCD_2.1/`, `Hardware/autodocs/`, and `Hardware/wiki.amigaos.net/`).

2. **Message structures**
   When sending a command to the Pi via JANUS:

   * Start with a message struct that embeds `struct Message` when using Amiga Exec ports, or a plain struct when sending directly into the ring.
   * Use **Amiga types**: `UWORD`, `ULONG`, `APTR`, etc.
   * For on‑wire structures shared with the Pi, avoid implicit padding:

     * Prefer fixed‑size fields in a defined order.
     * Use explicit big‑endian read/write helpers on the Pi side.

3. **Memory allocation**

   * Use `AllocVec()` / `FreeVec()` with appropriate flags (`MEMF_PUBLIC`, `MEMF_CLEAR` as needed).
   * For buffers shared with the Pi (e.g., fractal output), allocate in `MEMF_PUBLIC`.

4. **No floating‑point unless justified**

   * Default to fixed‑point or integer math in Amiga‑side code for portability and performance.
   * If floating‑point is required, be explicit about which math libraries and compiler flags are needed.

5. **Clang/GCC clean**

   * Code should compile with `-Wall -Wextra -Werror` once stable.
   * Avoid “clever” GCC extensions. No nested functions, no `typeof`, etc.

---

## 6. Coding guidelines — Host side (Pi / Linux / Windows)

Host‑side JANUS code lives under:

* `src/janus/pi/` for Pi‑local services and integration with the emulator.
* `src/janus/client/` for external clients (e.g. running on `homer` or other machines).

Goals:

1. **Standard C, no platform lock‑in**

   * Stick to C99/C11.
   * Keep OS‑specific code in thin wrappers (e.g. sockets, threads).

2. **Clang and GCC clean**

   * Code should compile without warnings under both compilers.
   * Use `-Wall -Wextra`, and keep the code warning‑free.
   * Always add (void) where we see `void process_janus_messages()` so it becomes `void process_janus_messages(void)` if no other variables are defined 

3. **Transport abstraction**
   Design host‑side code so that:

   * Local services can attach directly to the emulator integration on the Pi.
   * Remote services can connect via TCP/UDP or Unix domain sockets.
   * The JANUS protocol logic is not entangled with the transport layer.

4. **Endian handling**

   * Use explicit helpers for big‑endian fields when reading from / writing to structures that cross the Amiga/host boundary.
   * Avoid depending on `htonl`/`ntohl` semantics for internal structs; make conversions explicit.

---

## 7. JANUS security header

Security is required when connecting to remote hosts over IP.

The initial security model is deliberately simple:

* Each JANUS command that targets a remote host includes a **numeric secret** (e.g., 32‑ or 64‑bit value) in a fixed header.
* The remote service only accepts requests if the secret matches its configured value.
* This secret is not meant to be cryptographically strong yet; it is an access gate and can later be replaced or augmented.

Qwen should:

* Place security definitions (constants, structs, helper functions) in a **dedicated header**, e.g. `janus-security.h`, so the security layer can be swapped without touching every client.
* Ensure that all new remote commands carry the security field in their protocol struct.

Later, this header can be extended with:

* Nonces and replay protection.
* HMAC or other keyed hashes.
* Optional TLS or SSH tunneling configuration for remote links.

---

## 8. Remote features and transports

The JANUS fabric must support:

* Local co‑processor services (Pi‑only).
* Remote CPU/GPU/VM services reachable over IP.
* Different transports: TCP, UDP, Unix domain sockets, and pipes.

The contract for Qwen:

1. **Never hard‑code “localhost only” in the design**
   Always make the remote endpoint configurable (IP/hostname, port).

2. **Separate protocol from transport**

   * The JANUS message format and command semantics should not depend on whether the underlying transport is TCP or a local function call.
   * Implement thin adapters for TCP/UDP/sockets/pipes that feed the same core protocol handling code.

3. **Explicit configuration**

   * Remote endpoints should be configurable via config files and/or command‑line arguments, not compiled constants.
   * The Amiga side should be able to pass an IP/port or a symbolic name to the Pi side, which then resolves and connects.

Example usage patterns (conceptual, not code):

* `janus-mips --ip 172.16.0.2 --port 8888` on the Amiga, with a matching `janus-mips-client` on `homer` listening on that port.
* `janus-qemu --vm linux-dev --display-mode window` to talk to a QEMU instance via a local socket.

---

## 9. Existing documentation and references

This repository already contains comprehensive Amiga documentation under `Hardware/`:

* `Hardware/ADCD_2.1/` — full developer CDs: Hardware, Libraries, Includes & Autodocs.
* `Hardware/autodocs/` — text versions of key AmigaOS autodocs (exec, intuition, graphics, bsdsocket, etc.).
* `Hardware/wiki.amigaos.net/` — local mirror of the AmigaOS wiki.

Qwen should:

* Assume that correct AmigaOS API patterns, calling conventions, and subsystem interactions can be derived from these files.
* Follow documented guidelines, especially for:

  * `exec.library` (tasks, ports, messages, memory)
  * `intuition.library` (windows, events)
  * `graphics.library` (RastPort, BitMap, blitting)
  * `bsdsocket.library` (networking on Amiga)

Whenever generating or modifying Amiga code, Qwen should favor structures and patterns consistent with these manuals.

---

## 10. Vision: JANUS as a window to the world

The long‑term vision for JANUS in Kernel PiStorm64 is:

* The Amiga treats distant CPUs, GPUs, and VMs as **native‑feeling co‑processors**, accessed via small, well‑defined IPC contracts.
* Classic experiences (like Bridgeboard Janus windows) are reimagined with modern hardware: QEMU guests, Windows hosts, CUDA/OpenCL backends.
* All of this remains **transparent and respectful of the Amiga’s design**: Workbench‑friendly, Intuition‑friendly, and consistent with the original Amiga documentation.

Qwen’s role is to:

* Preserve this vision when refactoring or adding code.
* Keep ring usage lean and sane.
* Make remote integration transport‑agnostic and security‑aware.
* Ensure code remains portable, warning‑free, and faithful to the AmigaOS environment described in the manuals already shipped in this tree.

That is the contract.

