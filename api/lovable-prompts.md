# Lovable Prompt Pack: PiStorm64 Companion UI

Use these prompts in order. They are written to keep UI generation fast while protecting integration code.

## Guardrail Prompt (run first)
```text
Project context:
- React + TypeScript + Vite app in amiga-workbench-companion.
- Existing data/runtime wiring must be preserved.
- OpenAPI contract source is /home/smalley/pistorm64/api/openapi.json.

Hard constraints:
1) Do not remove or rewrite BLE, websocket, api-client, or hook logic in:
   - src/lib/ble.ts
   - src/lib/websocket.ts
   - src/lib/api-client.ts
   - src/hooks/use-pistorm.ts
2) Do not change endpoint paths or payload field names.
3) Keep current Workbench-inspired visual style and component consistency.
4) Add/adjust UI pages and components only, and call existing api-client methods where possible.
5) Keep mobile-first layout, fast tap targets, and deterministic loading/error/empty states.

Task:
Read the current UI and only propose additive/targeted edits for missing screens/flows.
```

## Prompt 1: App Shell + Navigation
```text
Create/refresh a consistent app shell for the connected state with sections:
- Home
- Storage
- Floppies
- Config
- Reset
- Audit

Requirements:
- Keep the existing connection flow screen untouched.
- Add a compact top status strip (CPU mode, ROM, memory summary, FPS, warning badge).
- Add a mini disk activity strip (controller + units) using existing status data.
- Ensure routes/components are mobile-first and readable on phone screens.
- Preserve existing theme/tokens and Workbench panel style.
- Do not change BLE/api/websocket internals.
```

## Prompt 2: Storage Page (PiSCSI 0..6)
```text
Implement Storage page UX using existing API client methods and types.

Page behavior:
- Show units 0..6 in fixed order.
- Per row show: unit, present, label, backend, RO/RW, size, activity.
- Actions:
  - Map (new flow dialog)
  - Unmap
  - Toggle RO/RW (implemented as map/remap flow)
  - Refresh

Map flow:
- Backend select: local / remote / block / cdrom
- Path/profile fields based on backend
- RW/RO toggle
- Confirm step

Error handling:
- If API returns 409, show “busy/queued” style state.
- Show clear inline errors and toast feedback.

Do not alter api-client endpoint definitions.
```

## Prompt 3: Floppies Page (A314 0..3)
```text
Implement Floppies page UX for units 0..3.

Per card:
- Unit id
- Inserted filename (if present)
- RW/RO
- Activity state

Actions:
- Insert
- Eject
- Replace
- Refresh

Insert/replace flow:
- Select image filename
- RW/RO toggle
- Confirm action

Use existing API client methods:
- getUiFloppyUnits
- postUiFloppyInsert
- postUiFloppyEject

Do not change BLE or websocket code.
```

## Prompt 4: Config Page (Guided + Raw)
```text
Create a two-mode Config page:
1) Guided mode (common keys / setvars summary)
2) Raw mode (full text editor workflow)

Workflow:
- Load config metadata/files
- Edit content
- Validate via /api/config/validate
- Show errors/warnings and derived summary
- Apply via /api/config/apply with stage/apply modes

UX requirements:
- Diff/dirty indicator before apply
- Validation must be explicit before apply button becomes primary
- Strong error surfacing and non-destructive defaults

Keep existing ConfigPanel behavior available until the new page is stable.
```

## Prompt 5: Reset Page + Safety
```text
Create Reset page with operations:
- Soft reset
- Hard reset
- Reboot Pi

Safety UX:
- Require explicit confirmation (type RESET or long-press interaction)
- Disable dangerous actions when activity indicates active I/O
- Show last related audit events in-page

Use existing api-client reset methods and preserve current guardrail behavior.
```

## Prompt 6: Audit + Realtime UX
```text
Improve audit and realtime UX:
- Dedicated Audit view with filters (action, result, latest count)
- Better event presentation (time, actor, action, status)
- Add visible “connection state” banner for websocket states:
  connected / reconnecting / disconnected

Important:
- Consume existing websocket event stream without changing protocol fields.
- Add UI handling for action.state events if present.
```

## Prompt 7: Polish + QA Pass
```text
Run a polish pass across all pages.

Checklist:
- Consistent spacing/typography/panel rhythm
- Loading, empty, error, and disabled states on every actionable panel
- Mobile viewport behavior and keyboard-safe inputs
- No visual regressions on current connection + status screens
- No data-layer rewrites

Deliver:
- A concise change summary by file.
- A punch list of anything still blocked by backend endpoints.
```

## Copy/Paste Validation Prompt (after each Lovable run)
```text
Before finalizing this iteration:
1) Confirm you did not modify:
   - src/lib/ble.ts
   - src/lib/websocket.ts
   - src/lib/api-client.ts
   - src/hooks/use-pistorm.ts
2) List every file changed.
3) For each changed file, state whether it is presentation-only or behavior-affecting.
4) Identify any assumptions made about backend responses.
```
