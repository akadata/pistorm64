# AmigaDOS Primer (Project Notes)

This project targets AmigaDOS, not Unix. Use Amiga assigns and conventions in docs/scripts/tools.

## Volumes and assigns

- **Devices/volumes:** `DF0:` (floppy), `DH0:`, `DH1:` (hard disk partitions), `SYS:` (current boot volume)
- **Assigns:** `C:`, `S:`, `LIBS:`, `DEVS:`, `L:`, `FONTS:`, `PREFS:`, `ENV:`, `ENVARC:`, `T:`

## Boot flow

- `S:Startup-Sequence` is the system boot script
- `S:User-Startup` is the preferred place for third‑party additions

## Where things live

- **CLI tools:** `C:`
- **Libraries (.library):** `LIBS:`
- **Handlers / filesystems:** `L:`
- **Mountfiles:** `DEVS:DosDrivers/`
- **RTG/monitor files:** `DEVS:Monitors/`
- **Network devices:** `DEVS:Networks/`
- **Prefs tools:** `SYS:Prefs/`

## Environment

- `ENV:` is volatile (RAM)
- `ENVARC:` is persistent (copied to `ENV:` at boot)

## Case sensitivity

AmigaDOS is case‑insensitive, but the host filesystem is not. Use canonical names in the repo:
`C`, `S`, `LIBS`, `DEVS`, `L`, `Prefs`, etc.

