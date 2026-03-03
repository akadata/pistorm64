# PiStorm Web Control Plan (Stage 2)

## 1) Scope And API Surface
- Keep **canonical control endpoints** under `/api/v1/*`.
- Keep **UI convenience endpoints** under `/api/*` and realtime at `/ws`.
- Treat `/api/*` as a stable dashboard contract; map internally to `/api/v1/*` handlers.
- Add explicit API version headers for forward compatibility:
  - Request: `X-Client-Version`
  - Response: `X-API-Version`

## 2) Security And Session Model
- BLE onboarding provides AP credentials and a short-lived bootstrap token.
- Exchange bootstrap token for bearer session token at first authenticated API call.
- Session token defaults:
  - TTL: 24h
  - idle timeout: 30m
  - revocation on pairing reset
- Read-only endpoints can remain open on AP (`/health`, `/api/status`, `/api/disks`, `/api/floppy`, `/ws`).
- All mutating endpoints require bearer token.
- Record token subject in audit logs for every mutation.

## 3) Mutation Queue And Concurrency
- All mutating operations return a queue/action id.
- Action states: `queued`, `running`, `done`, `failed`.
- If an operation conflicts with active I/O or lock state, return `409` with machine-readable error code.
- Support idempotency key on mutating calls (`Idempotency-Key`) to prevent accidental double-execution from flaky mobile networks.

## 4) WebSocket Contract
- `/ws` sends `status.snapshot` immediately after connect.
- Delta events:
  - `disk.activity`
  - `floppy.activity`
  - `warning`
  - `action.state`
- Add sequence number (`seq`) and server timestamp (`ts`) on every event.
- Reconnect behavior:
  - Client includes last seen `seq`.
  - Server either replays buffered events or forces full `status.snapshot`.

## 5) Storage And Floppy UX Rules
- PiSCSI units keep fixed index semantics (0..6) in UI.
- A314 floppies keep fixed unit semantics (0..3).
- Busy guardrail:
  - Unmap/eject denied during active operation unless forced mode is explicitly requested.
- All map/insert responses include normalized resolved source metadata (type, readonly, size, checksum if available).

## 6) Config Workflow Hardening
- Validate returns structured diagnostics:
  - `errors[]`, `warnings[]`, and source spans when available.
- Apply supports two-step mode:
  - `stage` writes temp + backup
  - `apply` promotes staged config atomically
- Keep rollback handle in response (`backup_path` or `backup_id`).
- Add endpoint for restore in next increment (`POST /api/v1/config/restore`).

## 7) Audit And Observability
- Audit entries for all mutating actions:
  - actor, action, target, request id, result, duration.
- Add lightweight metrics endpoint later (`/api/v1/system/metrics`) for queue depth, websocket client count, and recent error counters.

## 8) Delivery Phases
1. API consolidation
- Finalize `/api/v1/*` + `/api/*` + `/ws` contract in `api/openapi.json`.
- Generate TypeScript client for web UI.

2. Node control service skeleton
- HTTP router, auth middleware, action queue, websocket broadcaster.
- Stub adapters for PiSCSI/A314/config/reset.

3. Storage + floppy flows
- Implement map/unmap and insert/eject with busy handling.
- Emit activity and action-state events.

4. Config + reset flows
- Validate/stage/apply pipeline with backup metadata.
- Soft/hard/reboot actions + audit logging.

5. Mobile onboarding integration
- BLE bootstrap to AP + token exchange.
- Pairing reset rotates credentials and revokes sessions.

6. Hardening
- Fault injection tests (I/O busy, dropped websocket, power-loss during apply).
- End-to-end test script from phone/browser perspective.

## 9) Non-Negotiable Acceptance Criteria
- No config apply without successful validation (unless force flag is explicitly set and audited).
- Every mutation has an audit trail and action id.
- Web UI remains usable under websocket disconnect/reconnect.
- Busy conflicts are explicit (`409`) and user-visible.
- No operation blocks emulation/render loop.
