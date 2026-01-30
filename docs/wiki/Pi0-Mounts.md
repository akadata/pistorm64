# Pi0 Mounts

Pi0 mounts are provided through host-side sharing and A314/FS services. The recommended approach is to:

- Mount the Pi0 storage on the host.
- Expose it via `A314_SHARED` or other configured shares.
- Access it from Amiga via a handler or mounted volume.

See:
- `data/a314-shared/README.md`
- `a314/README.md`
