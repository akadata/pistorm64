# AGENTS.md

## Purpose

This repository implements a Zorro-visible ARM64 execution path for PiStorm-based Amiga systems.

The goal is to let the Amiga launch ARM64 payloads that execute natively on the Pi side through a stable mailbox/shared-memory contract, while keeping Amiga-side integration clean, minimal, and generic.

This is **not** a fake CPU replacement, not an emulator inside an emulator, and not a grab bag of app-specific hacks hidden in launcher code.

## Core Architecture

There are three distinct layers:

### 1. Zorro ARM64 accelerator device

The Zorro device exposes:

* AutoConfig presence
* MMIO registers
* shared memory window
* mailbox/job control
* completion/status reporting
* optional interrupt signalling

This layer is hardware-facing and protocol-facing.

It must remain generic.

### 2. Launcher / runtime bridge

This begins with `armshake` and later should be complemented or superseded by an Amiga `.library`.

Its job is to:

* detect the ARM64 device
* validate and load a payload
* upload payload and job data
* start execution
* poll or wait for completion
* provide diagnostics

This layer is **not** the application.

### 3. ARM64 payload

The payload is the application.

Examples:

* fractal renderer
* file manager
* editor
* game
* media tool
* OpenGL-style renderer using exported services

The payload owns its own logic, UI semantics, state, and behavior.

## Non-Negotiable Design Rules

### Rule 1: `armshake` must remain generic

`armshake` is a launcher and diagnostic tool.

It must never become the place where application logic is hidden.

`armshake` must never contain:

* app-specific menu IDs
* fractal type enums
* Julia/Mandelbrot selectors
* app-specific default values
* editor commands
* file manager logic
* game logic
* rendering semantics specific to one payload
* feature toggles that belong to a payload

If a new payload requires editing `armshake`, that is a design failure unless the change is purely generic ABI/service support.

### Rule 2: Payload-specific behavior lives in the payload

The ARM64 ELF owns:

* menus
* view state
* zoom logic
* reset logic
* per-app command handling
* rendering algorithm
* document model
* application state machines
* app-specific UI

A payload must be replaceable without rewriting launcher behavior.

### Rule 3: Diagnostics and execution must stay separate

Keep `armshake` as:

* a board probe tool
* a diagnostics tool
* a manual launcher
* a contract validation tool

That keeps it useful even after a `.library` exists.

### Rule 4: The long-term user-facing path is a library, not `armshake`

The intended direction is:

* `armshake` remains for diagnostics and development
* an Amiga `.library` becomes the normal execution interface

That `.library` should eventually:

* detect ARM payloads
* load metadata
* allocate job descriptors and shared buffers
* launch payloads
* expose helper APIs to Amiga programs
* support Workbench/tool integration

### Rule 5: File association belongs in metadata and library logic

Future payload discovery should be metadata-driven.

Preferred direction:

* `something.elf`
* `something.elf.info` or `something.info`
* optional payload metadata block or sidecar

The launcher/library should identify that a file is an ARM64 payload without needing per-application switches like `--julia`, `--editor`, or `--game`.

### Rule 6: Stable ABI first, features second

Only two contracts should grow carefully over time:

#### A. Launch ABI

How the Amiga side submits a payload and starts execution.

#### B. Service ABI

How the ARM payload requests generic services such as:

* framebuffer access
* window creation/update
* input events
* timers
* file services
* clipboard
* audio
* GPU/OpenGL-like services later

Everything else belongs outside the launcher.

### Rule 7: Never bind the system to the visible Zorro window size

The visible board aperture is not the true upper bound of the system.

Large payloads, large assets, large framebuffers, and streamed content must be supported through:

* chunking
* mailbox-controlled transfers
* paged/shared buffers
* host-side staging
* streaming protocols
* external file-backed or memory-backed transport

A 64K, 512K, or 4M board window must never define the final ceiling for:

* ELF size
* asset size
* framebuffer size
* package size
* future shared library size

### Rule 8: Generic service growth is allowed

Changes to launcher/library are valid when they add reusable infrastructure, for example:

* larger transfer support
* better shared-buffer handling
* event queue support
* generic windowing service
* generic blit/viewport service
* generic filesystem RPC
* generic GPU command submission

That is infrastructure.

Adding Julia-specific controls to launcher code is not infrastructure.

## What `armshake` is

`armshake` is:

* a diagnostic tool
* a launcher
* a contract exerciser
* a validation tool
* a development aid

Typical allowed commands include things such as:

* identify board
* inspect capabilities
* load ELF
* start job
* wait for completion
* reset job
* dump status
* trace mailbox state

## What `armshake` is never

`armshake` is never:

* an application framework for one demo
* a place to stash hidden business logic
* a menu controller for payloads
* a fractal chooser
* a file manager frontend
* an editor UI
* a game shell
* a substitute for the future `.library`

## Direction for `.library`

The desired Amiga-side `.library` should:

* auto-detect ARM64 payload files
* read metadata
* provide a clean API to launch payloads
* hide transport/setup details from userland
* allow Workbench and CLI integration
* keep payload execution generic

Possible future shape:

* `arm64exec.library`
* `arm64run.library`
* `pistormarm.library`

The name matters less than the boundary.

The boundary must remain clean.

## Review Gate for Future Changes

Before merging any change, ask:

1. Does this add generic execution/service infrastructure?
2. Or does this sneak application behavior into launcher code?

If the answer is the second one, do not merge it in that form.

## Practical Test

A correct design means:

* a new ARM64 `.elf` can be dropped in
* the same generic launcher/library can run it
* no new app-specific switch is needed
* no new app-specific enum is added to launcher code
* no launcher rewrite is needed for each application

When that is true, the system is real.

When that is not true, the architecture is drifting.

