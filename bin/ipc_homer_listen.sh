#!/usr/bin/env bash
set -euo pipefail
mkdir -p /tank/pistorm/ipc
rm -f /tank/pistorm/ipc/amiga.sock
exec socat -d -d UNIX-LISTEN:/tank/pistorm/ipc/amiga.sock,fork \
  SYSTEM:'ts "[%Y-%m-%d %H:%M:%S]" | tee -a /tank/pistorm/ipc/amiga_markers.log'