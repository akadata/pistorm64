# Single Lovable Prompt (Full UI Pass)

```text
You are updating an existing React + TypeScript + Vite app at:
/home/smalley/pistorm64/amiga-workbench-companion

Goal:
Deliver a complete, uniform, mobile-first PiStorm64 web UI for:
- Home
- Storage
- Floppies
- Config
- Reset
- Audit

Reference API contract:
/home/smalley/pistorm64/api/openapi.json

Critical guardrails (must follow):
1) Do NOT modify these files:
   - src/lib/ble.ts
   - src/lib/websocket.ts
   - src/lib/api-client.ts
   - src/hooks/use-pistorm.ts
2) Do not rename or change endpoint paths/payload field names.
3) Keep existing Workbench-inspired design language, spacing rhythm, and tokens.
4) Preserve current connection flow behavior.
5) Prefer additive/targeted UI changes over broad rewrites.

Deliverables:
1) App shell and navigation for connected state:
   - Sections: Home, Storage, Floppies, Config, Reset, Audit
   - Keep connection screen intact for disconnected state.

2) Home view:
   - Top status card: CPU mode, ROM, memory summary, FPS, warning state.
   - Mini disk activity strip for controller + units using existing live status data.
   - Quick action links/cards to other sections.

3) Storage (PiSCSI units 0..6):
   - Fixed row order 0..6.
   - Row fields: unit, present, label, backend, RW/RO, size, activity.
   - Actions: Map, Unmap, Toggle RO/RW (via remap flow), Refresh.
   - Map flow supports local/remote/block/cdrom inputs.
   - Handle 409 busy conflicts with clear UI state and message.

4) Floppies (A314 units 0..3):
   - Fixed cards DF0..DF3.
   - Show inserted filename, RW/RO, activity.
   - Actions: Insert, Eject, Replace, Refresh.
   - Insert/replace flow: filename + RW/RO + confirm.

5) Config:
   - Two modes: Guided and Raw.
   - Raw flow: edit -> validate -> show errors/warnings/derived -> stage/apply.
   - Dirty state indicator.
   - Conservative defaults (no destructive apply shortcuts).

6) Reset:
   - Actions: soft, hard, reboot pi.
   - Safety confirm gate (type RESET or long-press).
   - Disable/reset guardrails when active I/O state indicates busy.
   - Show recent related audit events.

7) Audit:
   - Dedicated list view with status coloring and readable timestamps.
   - Optional simple filtering (result/action text).

8) Realtime UX:
   - Show websocket state banner (connected/reconnecting/disconnected).
   - Use existing events for status/activity updates.
   - Add action queue/status rendering if action.state events are present.

Quality requirements:
- Every panel has loading, empty, error, and disabled states.
- Mobile-first, touch-friendly controls.
- Keep component consistency across sections.
- No regressions to existing connected/disconnected flow.

Final output format required:
1) List all changed files.
2) For each file: “presentation-only” or “behavior-affecting”.
3) Explicitly confirm none of these were changed:
   src/lib/ble.ts, src/lib/websocket.ts, src/lib/api-client.ts, src/hooks/use-pistorm.ts
4) List backend/API assumptions or blockers.
```

