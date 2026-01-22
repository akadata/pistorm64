Pi-side Janus helpers
=====================

Pi-side debug and service utilities will live here.

Current status:
- Ring-buffer debug is wired into pistorm-dev as a temporary hook.
  The doorbell uses PI_DBG_VAL1 to trigger a ring dump.

Next:
- Move ring decode into a standalone service for QEMU/DOSBox bridging.
