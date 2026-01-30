# Changes and Comparison

This repository tracks PiStorm64 improvements relative to upstream projects. Use this page to record:

- Upstream base (repo + commit/tag).
- Local diffs (autoconfig, PiSCSI/A314 improvements, DMA safety, ROM protections, logging, performance work).
- Any kernel module changes in your deployment environment.

Suggested workflow:

- Compare against upstream using:
```
git log --oneline --decorate
```
- Note feature deltas here when merged.

Related files:
- `README.md`
- `platforms/amiga/readme.md`
- `platforms/amiga/piscsi/readme.md`
