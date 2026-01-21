Codex Task Brief: Kernel PiStorm64 Janus Bus Engine
Current truth

Profiles exist and are active/loaded.

Disk manager exists and disks are linked to a profile (DF0–DF3 / PD0–PD3).

RemoteWB works (websocket + bpls2gif compiled module).

A314 disk service is running and PD0 mounts.

Goal

Turn the existing config UI into a tabbed, branded Janus control plane:

Control (start/stop/restart services, “reboot Amiga” = systemd restart)

Profiles (create/load/save/activate)

Disks (ADF library scan, attach per-profile, hot swap lists)

RemoteWB (embedded viewer / status, optional launch)

Editor (file manager + code editor)

Build/Run (compile Amiga binaries + package outputs)

AI (configure providers + keys + models; connect to local Ollama, optional OpenAI endpoint)

System (package info, logs, versions, “health”)

1) Systemd “Reboot Amiga” = Restart kernelpistorm64
Requirements

Web UI button triggers a systemd restart of the service that runs the emulator/kernel.

Provide status: active/inactive, last restart time, last exit code, journal tail.

Implementation plan

Confirm the service name (example): kernelpistorm64.service

Add backend endpoints:

POST /api/v1/systemd/restart/kernelpistorm64

GET /api/v1/systemd/status/kernelpistorm64

GET /api/v1/systemd/journal/kernelpistorm64?lines=200

Backend execution options (pick one):

Preferred: backend runs as root via systemd service + local socket; no sudo from web code.

Alternate: sudoers rule for only these commands:

/bin/systemctl restart kernelpistorm64.service

/bin/systemctl status kernelpistorm64.service --no-pager

/bin/journalctl -u kernelpistorm64.service -n 200 --no-pager

Safety

Restart endpoint must be authenticated.

Add a 2-step confirm (UI): “Restart emulator (Amiga will reboot)”.

2) Web UI: tabbed, aligned, skinnable, logo-ready
UI layout

Top bar:

Left: Kernel PiStorm64 + logo area (use KernelPistorm64.png)

Center: Tabs

Right: status pills:

Emulator: Running/Stopped

Amiga link: Connected/Disconnected

A314 Disk: Running/Down

RemoteWB: Running/Down

Tabs:

Control

Profiles

Disks

RemoteWB

Editor

Build

AI

System

Styling requirements

Everything aligns to a grid (labels fixed width, fields same height).

“Dirty state” indicator for profile changes.

Theme support:

theme=classic, theme=workbench, theme=dark

simple CSS override file loaded from /etc/kernelpistorm64/theme.css or similar.

Workbench-inspired skin (later)

Optional theme that mimics Workbench window chrome without copying copyrighted assets.

3) API design (simple, secure, stable)
Base

http://pi:PORT/api/v1/...

JSON only.

Consistent responses:

{ "ok": true, "data": ... }

{ "ok": false, "error": { "code": "...", "message": "..." } }

Authentication (lightweight but real)

Header-based key:

X-API-Key: <key>

X-API-Secret: <secret>

Stored in /etc/kernelpistorm64/api-keys.json (0600) or env file.

Rate limit basic actions (restart/build) to avoid accidental loops.

Core endpoints (minimum)

Profiles

GET /api/v1/profiles

GET /api/v1/profiles/{name}

POST /api/v1/profiles (create)

PUT /api/v1/profiles/{name} (save)

POST /api/v1/profiles/{name}/activate

Disks

GET /api/v1/disks/library?path=...

POST /api/v1/disks/eject { "drive": 0 }

POST /api/v1/disks/insert { "drive": 0, "path": "/.../blank.adf" }

GET /api/v1/disks/status

POST /api/v1/disks/swaplist { "profile": "x.cfg", "list": [...] }

Systemd

GET /api/v1/systemd/status/{unit}

POST /api/v1/systemd/restart/{unit}

GET /api/v1/systemd/journal/{unit}

RemoteWB

GET /api/v1/remotewb/status

POST /api/v1/remotewb/start

POST /api/v1/remotewb/stop

Files

GET /api/v1/fs/list?path=/home/...

GET /api/v1/fs/read?path=/home/.../file.c

PUT /api/v1/fs/write { "path": "...", "content": "..." }

POST /api/v1/fs/mkdir { "path": "..." }

POST /api/v1/fs/upload (multipart)

POST /api/v1/fs/delete { "path": "..." }

Path jail: only allow within configured roots (see next section)

Build

POST /api/v1/build/amiga-gcc { "project": "...", "target": "...", "args": [...] }

GET /api/v1/build/jobs

GET /api/v1/build/jobs/{id} (logs + status)

POST /api/v1/run/vamos { "binary": "...", "args": [...] } (optional, controlled)

AI

GET /api/v1/ai/config

PUT /api/v1/ai/config

POST /api/v1/ai/chat (server-side proxy to Ollama/OpenAI; never expose keys to browser)

4) File roots + project structure (so “packages” are clean)
Define safe roots

WORK_ROOT=/home/smalley/pistorm64/work

ADF_ROOT=/home/smalley/pistorm64/data/adfs

SHARED=/home/smalley/pistorm64/data/a314-shared

Backend must refuse access outside these roots.

Project layout proposal

work/projects/<name>/src (Amiga sources)

work/projects/<name>/include

work/projects/<name>/build

work/projects/<name>/dist (artifacts to copy into SHARED or ADF)

work/projects/<name>/project.json (build recipe)

5) Web editor (minimum viable)
Features

Left pane: file tree (within WORK_ROOT/projects)

Main pane: Monaco editor or similar

Buttons:

Save

New file / New folder

Upload

Download

File types: .c .h .i .s .asm .txt .cfg .md

Optional: syntax highlighting, search, quick open.

Security

No arbitrary shell from browser.

All build/run happens via controlled backend commands.

6) Build pipeline (Amiga GCC + VASM + packaging)
Minimum

A build job runner that:

runs in a controlled environment

captures stdout/stderr

writes logs per job

outputs artifacts into dist/

Typical toolchain commands supported

m68k-amigaos-gcc

vasmm68k_mot

vlink (if needed)

amitools utilities (adf create/write etc.)

vamos / machine68k (optional runtime testing)

Packaging targets

Copy artifact into a314-shared/workbench/ (or per-project folder)

Optional: write into an ADF in ADF_ROOT using amitools

7) AI integration (pluggable providers, no drama)
Principle

Browser never talks to OpenAI/Ollama directly. Browser talks to Janus API, which proxies.

Providers to support

Local Ollama (host+port, model name)

OpenAI-compatible endpoint (base_url + key + model)

“Codex/Qwen” are treated as “models/endpoints” via the same proxy shape (even if used differently later)

Config fields

provider: ollama | openai_compatible

base_url

api_key (server-side only)

model

temperature, max_tokens

optional: system prompt for coding style

Editor assistant actions (first wave)

“Explain file”

“Propose patch”

“Apply patch” (creates a git diff preview; user must click Apply)

8) MCP + “Lovable.dev frontend build” readiness

To let an external UI generator build against this cleanly:

Provide an openapi.json at:

GET /api/v1/openapi.json

Keep endpoints stable and named well.

Provide example requests in /docs/API_EXAMPLES.md.

9) What to include next (don’t miss these)

Log viewer tab:

emulator logs

a314 logs

remotewb logs

Health tab:

CPU load, RAM, disk usage

network status

Backups:

export profile bundle (cfg + disks list)

import bundle

A tight “next 3 steps” execution order

Systemd control endpoints + UI Control tab (restart kernelpistorm64 + status + journal)

Disks API + UI Disks tab (ADF scan, insert/eject, per-profile mapping, swap list)

Editor + Files API + Build jobs (file manager, compile, drop artifacts into shared)
