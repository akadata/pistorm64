# ARMAccel Service ABI v1 (Contract Freeze Draft)

## Canonical Model

- App artifact is AArch64 payload ELF.
- Payload instructions execute on coprocessor side.
- AmigaOS services remain Amiga-side (Exec/DOS/Intuition/devices/libraries).
- Payload accesses AmigaOS services only through ARMAccel Service ABI.
- `armrun` is a thin launcher stub only.

This is a coprocessor service model, not a remote-app runtime model.

## Primary Contract Artifacts

- `src/platforms/amiga/zorro/arm64_accel/armaccel_abi_types_v1.h`
- `src/platforms/amiga/zorro/arm64_accel/armaccel_service_abi.h`
- `abi.md` (progress checklist)

## Wire Rules

- Service frame location: AJOB extension at `0xC0`, size `0x40`.
- Frame fields are BE32 on wire.
- Structured blobs must declare endianness via `armaccel_blob_desc_v1.endian`.
- String encoding must be declared (`ASCII`, `UTF-8`, `AMIGA8`).

## Numeric Conventions (Frozen for v1)

- Coordinates: signed `s32` pixel units.
- Sizes/dimensions: unsigned `u32` pixel units.
- Stride: unsigned `u32` bytes.
- Handles: unsigned `u32` (`0` invalid).
- File offsets: 64-bit split `hi/lo` (`u32` each, hi first).
- Time for sleep APIs: integer milliseconds.
- Event timestamps: integer ticks (optional feature, tick rate via caps).
- Core ABI arithmetic: integer-only; fixed-point is not required in core ABI.

## Session and Handle Model

- `SESSION_OPEN` creates a session handle (`result1`).
- Handles are per-session and `0` is always invalid.
- Closing a session invalidates all child handles.
- Handle values may be recycled by runtime after close.

Handle classes are explicit (`SESSION/WINDOW/MENU/REQUESTER/SURFACE/FILE/TIMER/DEVICE`).

## Argument + Blob Model

Small calls use `arg0..arg3`.
Structured calls use shared-memory blobs:

- `arg0 = blob offset`
- `arg1 = blob length`
- `arg2 = blob format/version`
- `arg3 = flags`

`armaccel_blob_desc_v1` defines offset/size/capacity/flags/format/endianness/encoding.

## Event Model

- Event ring descriptor: `armaccel_event_ring_v1`.
- Event record: `armaccel_event_v1`.
- Event classes include completion/window/input/menu/requester/timer/file/device.

## Capability Negotiation

- Caps record: `armaccel_caps_v1`.
- Reports service caps, profile caps, feature flags, pixel-format caps, and limits.

Profiles currently named:

- `CORE`
- `APP`
- `RENDER`
- `BATCH`
- `GUEST`

## Async Baseline

- One in-flight request per session in v1 baseline.
- Async and non-blocking flags exist.
- Completion may be via frame state transition and/or completion events.

## Service Families (Named)

- Session
- Window
- Menu
- Requester
- Surface
- Input
- File
- Timer
- Clipboard
- Audio (seeded)
- Video stream (seeded)
- Device I/O mediated (seeded)
- Block I/O (seeded)
- Net I/O (seeded)
- DataTypes (seeded)

## Typed Request Structs (Seeded)

- `armaccel_session_open_v1`
- `armaccel_window_open_v1`
- `armaccel_surface_desc_v1`
- `armaccel_file_open_v1`
- `armaccel_file_io_v1`
- `armaccel_menu_set_v1`
- `armaccel_requester_open_v1`
- `armaccel_datatype_request_v1`

## Error Namespace Discipline

- `result0` = generic ABI result class.
- `result1` = namespace-packed detail (`DOS`, `INTUITION`, `EXEC`, `DEVICE`, etc.).

## Implemented Baseline

Implemented in runtime path (`armaccel.library` + `armaccel.device` cooperative hook during `RUN_ELF`):

- session open/close/get caps
- window open/close
- surface alloc/free
- `SURFACE_PRESENT` opcode is currently stubbed and not semantically frozen
- input poll
- message requester open
- file open/read/close
- timer sleep/get tick
- event ring enqueue for window/input events

## Frozen for v1 (Contract Shape)

- core numeric conventions and wire model
- session/handle/frame/blob/event/caps structures
- service/opcode namespace for current families
- embedded ELF NOTE identity requirements

## Not Yet Frozen — Blocks Stable Third-Party v1

- normative menu tree binary schema
- requester payload/result schemas beyond message baseline
- surface present semantics (copy/flip/reference, resize coupling, dirty-rect rules)
- DOS semantic mapping details (locks/examine/path/EOF/share/error translation)
- async cancellation and completion acknowledgement lifecycle
- per-opcode struct revision compatibility table

## Deferred Beyond Initial Usable Milestone

- audio service contracts
- video stream contracts
- mediated raw device/library ABI safety contracts
- block I/O and net I/O service contracts
- DataTypes semantic contracts (`identify/load/convert/metadata/save`)
- extended DOS surface beyond baseline open/read/close
- richer clipboard/security policy extensions

## Validation Payload

- `amiga/zorro-arm64/arm64-payloads/abi-smoke.elf` (`build_abi_smoke_elf.sh`)

## Rule

No payload-specific behavior belongs in `armrun`.
All app semantics must live in payload + generic service ABI.
