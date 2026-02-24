# net64.device vs SANA-II Revision 7 Checklist

Source reference: `src/platforms/amiga/net64/net_driver_amiga/doc/SANA-II_Revision_7`
Implementation reviewed: `src/platforms/amiga/net64/net_driver_amiga/net64-amiga.c`

Legend:
- `Implemented`: command exists and has functional behavior.
- `Partial`: command exists but behavior is simplified/incomplete vs Rev7 semantics.
- `Missing`: no explicit handling; currently falls through to unsupported command path.

## Core I/O Commands

| Command | Status | Notes |
|---|---|---|
| `CMD_READ` | Implemented | Reads frame via NET64 RX registers and copy callback path. |
| `CMD_WRITE` | Implemented | Writes frame via NET64 TX registers and copy callback path. |
| `S2_BROADCAST` | Implemented | Maps to write with broadcast destination. |
| `S2_MULTICAST` | Missing | Not handled explicitly. |
| `S2_READORPHAN` | Partial | Implemented with tracked-type skip loop; simplified behavior vs full queue semantics. |
| `S2_READMGMT` | Missing | Not implemented. |
| `S2_WRITEMGMT` | Missing | Not implemented. |

## Interface/State Commands

| Command | Status | Notes |
|---|---|---|
| `S2_DEVICEQUERY` | Partial | Basic fields returned; no NSD command introspection path in this device file. |
| `S2_GETSTATIONADDRESS` | Implemented | Returns current station address in src/dst fields. |
| `S2_CONFIGINTERFACE` | Partial | Address set supported; no full validation/reporting matrix. |
| `S2_ONLINE` / `S2_OFFLINE` | Partial | State toggled; does not enforce full Rev7 connected/disconnected state rules. |
| `S2_CONNECT` / `S2_DISCONNECT` | Partial | Simple state toggles; no `Sana2Connection` hook lifecycle per Rev7. |
| `S2_ONEVENT` | Partial | Event flags returned/cleared; no wait semantics and limited event coverage. |

## Statistics Commands

| Command | Status | Notes |
|---|---|---|
| `S2_GETGLOBALSTATS` | Implemented | Basic stats populated. |
| `S2_GETTYPESTATS` | Partial | Returns totals, not truly per tracked packet type yet. |
| `S2_GETSPECIALSTATS` | Partial | Returns empty header only. |
| `S2_GETEXTENDEDGLOBALSTATS` | Partial | Zero-filled placeholder only. |
| `S2_SAMPLE_THROUGHPUT` | Partial | Zero-filled placeholder only. |

## Type Tracking Commands

| Command | Status | Notes |
|---|---|---|
| `S2_TRACKTYPE` | Implemented | Added tracked-type table and duplicate checks. |
| `S2_UNTRACKTYPE` | Implemented | Removes tracked types with not-tracked error handling. |

## Multicast Control Commands

| Command | Status | Notes |
|---|---|---|
| `S2_ADDMULTICASTADDRESS` | Missing | Not implemented. |
| `S2_DELMULTICASTADDRESS` | Missing | Not implemented. |

## Session/Address Extension Commands

| Command | Status | Notes |
|---|---|---|
| `S2_GETPEERADDRESS` | Partial | Returns zero addresses. |
| `S2_GETDNSADDRESS` | Partial | Returns zero addresses. |
| `S2_GETNETWORKS` | Missing | Not implemented. |
| `S2_GETNETWORKINFO` | Missing | Not implemented. |
| `S2_GETSIGNALQUALITY` | Missing | Not implemented. |
| `S2_SETOPTIONS` | Missing | Not implemented. |
| `S2_SETKEY` | Missing | Not implemented. |
| `S2_SANA2HOOK` | Missing | Not implemented. |

## Key Rev7 Semantics Still Missing/Incomplete

1. Promiscuous mode exclusivity and open policy (`SANA2OPF_PROM`) enforcement.
2. Proper asynchronous event wait semantics for `S2_ONEVENT` and full event set coverage.
3. Full `S2_CONNECT`/`S2_DISCONNECT` `Sana2Connection` hook flow and error/reporting semantics.
4. True per-packet-type counters for `S2_GETTYPESTATS`.
5. Multicast control and `S2_MULTICAST` command path.
6. Management frame paths (`S2_READMGMT`/`S2_WRITEMGMT`).
7. `S2_SANA2HOOK` and extended copy/log hook negotiation support.
8. NSD-style capability reporting (`NSCMD_DEVICEQUERY`) and coherent advertised command mask.

## Practical Guidance

- For current bring-up, the implemented subset is usually enough for basic IP stack traffic.
- For strict Rev7 compatibility and fewer edge-case hangs, the next high-priority work is:
  1) `S2_MULTICAST` + add/del multicast commands,
  2) per-type stats correctness,
  3) `S2_ONEVENT` wait semantics + event fidelity,
  4) `S2_CONNECT`/`S2_DISCONNECT` hook-compliant lifecycle.
