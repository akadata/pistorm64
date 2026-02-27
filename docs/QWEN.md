# QWEN.md

## Objective

You are operating inside the pistorm64 repository.

Your task is to fix all GitHub CodeQL security issues related to:

"Uncontrolled data used in path expression"

These issues affect multiple Python files under:

* src/.../files_pi/
* src/.../picmd/
* src/.../libremote/
* src/.../libremote_pistorm/
* src/.../a314fs/
* bin/

The goal is to secure path handling without breaking emulator behaviour.

---

## FIRST ACTION

Run:

```
./view_issues.sh
```

This outputs JSON describing all current CodeQL findings.

Parse this JSON and systematically fix each issue.

---

## CRITICAL CONSTRAINTS

1. DO NOT change emulator logic.
2. DO NOT change protocol structures.
3. DO NOT modify endian handling (BE/LE conversions must remain exactly as implemented).
4. DO NOT change socket behaviour.
5. DO NOT change file formats.
6. DO NOT introduce behavioural changes.

This is a hard security pass, not a refactor.

---

## What CodeQL Means by "Uncontrolled data used in path expression"

Typically this means:

* User input or socket data is directly concatenated into a filesystem path.
* Path traversal ("../") is not prevented.
* Absolute paths may be allowed.
* No validation against a fixed base directory.

Example of insecure pattern:

```
open(base_dir + "/" + user_input)
```

Example of secure pattern:

```
from pathlib import Path

base = Path(base_dir).resolve()
target = (base / user_input).resolve()

if not str(target).startswith(str(base)):
    raise SecurityError("Path escape detected")

open(target)
```

---

## Required Fix Pattern

For each path issue:

1. Identify the base directory (must already exist in code).
2. Use pathlib.Path for all path joins.
3. Resolve the final path.
4. Enforce containment inside the base directory.
5. Reject absolute paths.
6. Reject path traversal.

Use a shared helper if appropriate:

```
def secure_path(base_dir, user_path):
    from pathlib import Path
    base = Path(base_dir).resolve()
    target = (base / user_path).resolve()
    if not str(target).startswith(str(base)):
        raise ValueError("Path traversal detected")
    return target
```

Then replace unsafe joins with secure_path usage.

---

## Important Context

These Python modules interact with:

* The emulator
* A314 filesystem bridge
* RemoteWB
* Socket-based commands
* Disk image access

Many paths originate from Amiga-side requests.

We must assume hostile input.

However, behaviour must remain identical for valid inputs.

---

## What NOT To Do

* Do not rewrite entire modules.
* Do not restructure architecture.
* Do not add new dependencies.
* Do not remove functionality.
* Do not touch C code.
* Do not touch endian code.

---

## Expected Outcome

After fixes:

1. ./view_issues.sh should report zero uncontrolled path findings.
2. Emulator should behave identically for valid inputs.
3. No BE/LE changes.
4. No protocol changes.

---

Proceed methodically.

Security first.
Stability preserved.
Performance unchanged.

End of instructions.

