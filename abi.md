# ARMAccel ABI Tracking Checklist

Single source of progress tracking for service ABI stabilization.

Legend:
- `[x]` complete and named in contract
- `[ ]` not complete yet

Primary contract files:
- `src/platforms/amiga/zorro/arm64_accel/armaccel_abi_types_v1.h`
- `src/platforms/amiga/zorro/arm64_accel/armaccel_service_abi.h`
- `docs/armaccel_service_abi_v1.md`

## Core Shape

- [x] Session model exists
  - Names: `ARMACCEL_SVC_SESSION_*`, `armaccel_session_open_v1`
  - Rules named: per-session scope, close invalidates child handles
- [x] Handle model exists
  - Names: `armaccel_svc_handle_t`, `ARMACCEL_HANDLE_INVALID`, `ARMACCEL_HANDLE_CLASS_*`
  - Rules named: `0` invalid, handles recyclable after close
- [x] Argument+blob descriptor convention exists
  - Names: `armaccel_blob_desc_v1`, `ARMACCEL_BLOB_FLAG_*`, `ARMACCEL_BLOB_ENDIAN_*`
- [x] Foundational wire structs exist
  - Names:
    - `armaccel_svc_frame_v1`
    - `armaccel_blob_desc_v1`
    - `armaccel_event_v1`
    - `armaccel_caps_v1`
    - `armaccel_window_open_v1`
    - `armaccel_surface_desc_v1`
    - `armaccel_file_open_v1`

## Memory/Encoding/Versioning

- [x] Wire frame endianness rules named
  - Names: frame fields BE32, blob endianness flags in header
- [x] String encoding enums named
  - Names: `ARMACCEL_STR_ENC_ASCII`, `ARMACCEL_STR_ENC_UTF8`, `ARMACCEL_STR_ENC_AMIGA8`
- [x] ABI major/minor versioning named
  - Names: `ARMACCEL_SVC_ABI_MAJOR`, `ARMACCEL_SVC_ABI_MINOR`
- [x] Numeric conventions frozen in contract
  - Names: `ARMACCEL_COORD_FORMAT_S32`, `ARMACCEL_SIZE_FORMAT_U32`, `ARMACCEL_TIME_UNIT_MS`, `ARMACCEL_TIME_UNIT_TICKS`, `ARMACCEL_U64_HI/LO/MAKE`
- [x] Core ABI fixed-point policy frozen
  - Names: `ARMACCEL_CORE_FIXEDPOINT_NONE`
- [ ] Service-struct per-opcode versioning policy fully frozen
  - Missing: explicit compatibility table per struct/opcode revision

## Events/Async/Caps

- [x] Event queue/ring model named
  - Names: `armaccel_event_ring_v1`, `armaccel_event_v1`, `ARMACCEL_EVT_*`
- [x] Event timestamp convention named
  - Names: `armaccel_event_v1.tick_hi/tick_lo`, `ARMACCEL_TIME_UNIT_TICKS`, `armaccel_caps_v1.tick_hz_hi/tick_hz_lo`
- [x] Async/in-flight baseline rule named
  - Names: `ARMACCEL_SVC_FLAG_ASYNC`, `ARMACCEL_SVC_FLAG_NONBLOCK`, `ARMACCEL_SVC_MAX_INFLIGHT_PER_SESSION`
- [x] Capability negotiation struct named
  - Names: `armaccel_caps_v1`, `ARMACCEL_FEAT_*`, `ARMACCEL_PROFILE_*`, `ARMACCEL_CAP_*`
- [ ] Async completion/cancel/overlap behavior fully frozen
  - Missing: cancellation opcodes and completion acknowledgement lifecycle rules

## Service Families

- [x] Window service request struct named
  - Names: `armaccel_window_open_v1`, `ARMACCEL_WINDOW_FLAG_*`, `ARMACCEL_SVC_WINDOW_*`
- [x] Surface service request struct named
  - Names: `armaccel_surface_desc_v1`, `ARMACCEL_PIXFMT_*`, `ARMACCEL_SVC_SURFACE_*`
- [x] File service request structs named
  - Names: `armaccel_file_open_v1`, `armaccel_file_io_v1`, `ARMACCEL_SVC_FILE_*`
- [x] Menu/requester request structs named
  - Names: `armaccel_menu_set_v1`, `armaccel_requester_open_v1`, `ARMACCEL_SVC_MENU_*`, `ARMACCEL_SVC_REQUESTER_*`
- [x] Audio/video/device/disk/net class IDs seeded
  - Names: `ARMACCEL_SVC_AUDIO`, `ARMACCEL_SVC_VIDEO`, `ARMACCEL_SVC_DEVICE_IO`, `ARMACCEL_SVC_BLOCK_IO`, `ARMACCEL_SVC_NET_IO`
- [x] DataTypes class ID seeded
  - Names: `ARMACCEL_SVC_DATATYPE`, `ARMACCEL_CAP_DATATYPE`, `armaccel_datatype_request_v1`
- [ ] Menu tree binary format frozen
  - Missing: concrete menu blob schema and state/update semantics
- [ ] Requester typed payload/result formats frozen
  - Missing: per-requester-type schema definitions
- [ ] Surface present semantics fully frozen
  - Missing: copy/flip/reference semantics, resize coupling, dirty-rect rules
- [ ] DOS semantic mapping frozen
  - Missing: lock/examine/path/EOF/share semantics and exact error mappings
- [ ] Raw device/library mediated ABI frozen
  - Missing: allowed families, safety constraints, request structure schemas
- [ ] DataTypes ABI frozen
  - Missing: typed schemas and semantics for identify/load/convert/metadata/save

## Error/Security

- [x] Error namespace discipline seeded
  - Names: `ARMACCEL_SVC_ERRNS_*`, `ARMACCEL_SVC_ERR_PACK`, `ARMACCEL_SVC_RES_*`
- [ ] Service-specific subcode catalogs frozen
  - Missing: per-service code tables and translation rules
- [ ] Security/trust model frozen
  - Missing: explicit policy (fully trusted vs constrained capabilities)

## Profile Split

- [x] Core/App/Render/Batch/Guest profile bits named
  - Names: `ARMACCEL_PROFILE_CORE`, `ARMACCEL_PROFILE_APP`, `ARMACCEL_PROFILE_RENDER`, `ARMACCEL_PROFILE_BATCH`, `ARMACCEL_PROFILE_GUEST`
- [ ] Normative profile requirements frozen
  - Missing: required service sets and behavioral guarantees per profile

## Milestone Classification

### Implemented Baseline

- [x] Thin launcher boundary enforced (`armrun` stub only)
  - File: `amiga/zorro-arm64/C/armrun.c`
- [x] Runtime service dispatcher baseline integrated on service frame path
  - Files:
    - `amiga/zorro-arm64/C/armaccel_library.c`
    - `amiga/zorro-arm64/C/armaccel_device.c`
    - `amiga/zorro-arm64/C/armaccel_iocmds.h`
- [x] Minimal service subset wired
  - `SESSION_OPEN/CLOSE/GET_CAPS`
  - `WINDOW_OPEN/CLOSE`
  - `SURFACE_ALLOC/FREE`
  - `SURFACE_PRESENT` opcode is currently stubbed and not semantically frozen
  - `INPUT_POLL`
  - `REQUESTER_OPEN` (message requester baseline)
  - `FILE_OPEN/READ/CLOSE`
  - `TIMER_SLEEP_MS/GET_TICK`
  - event ring enqueue for window/input events
- [x] AArch64 proof payload for baseline slice
  - Files:
    - `amiga/zorro-arm64/arm64-payloads/abi_smoke_arm64.c`
    - `amiga/zorro-arm64/arm64-payloads/build_abi_smoke_elf.sh`
  - Output: `abi-smoke.elf`

### Frozen for v1 (Contract Shape)

- [x] Core numeric conventions, handle/session model, frame/blob/event/caps structs
- [x] Service family IDs and baseline opcode namespace
- [x] Embedded ELF NOTE identity requirement

### Not Yet Frozen — Blocks Stable Third-Party v1

- [ ] Normative menu tree binary schema
- [ ] Requester typed payload/result schemas beyond message baseline
- [ ] Surface present semantics (copy/flip/reference, resize coupling, dirty-rect rules)
- [ ] DOS semantic mapping details and error translation guarantees
- [ ] Async cancellation/completion acknowledgement lifecycle
- [ ] Per-opcode struct revision compatibility policy/table

### Deferred Beyond Initial Usable Milestone

- [ ] Audio service contracts
- [ ] Video stream service contracts
- [ ] Mediated raw device/library access contracts
- [ ] Block I/O and net I/O contracts
- [ ] DataTypes semantic contracts
- [ ] Extended DOS operations beyond baseline open/read/close
