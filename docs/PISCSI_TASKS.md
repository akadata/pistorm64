# PiSCSI Warning Cleanup Tasks

## Goal
Bring `src/platforms/amiga/piscsi/piscsi.c` to 0 warnings in `core-warnings.log`

## Constraints
- Do not touch `src/musashi/*` or `src/softfloat/*`
- Keep behaviour unchanged
- Localized fixes only
- Follow bottom-up (newest-to-oldest) fixing method so line numbers match

## Method
Fix warnings bottom-up (newest-to-oldest) so line numbers match between warning output and source code.

## Workflow Steps
1. Run `./rebuild.sh` to build with clang and capture warnings
2. Run `./rebuildcount.sh piscsi.c` to see warnings specific to piscsi.c
3. Fix warnings iteratively until `./rebuildcount.sh` reports:
   - piscsi.c: 0
4. Move to the next file per AGENTS.md order

## Verification
- After each fix iteration, run `./rebuild.sh` and `./rebuildcount.sh piscsi.c` to confirm progress
- Ensure the `pistorm` module reloads correctly
- Preserve all existing emulator behavior