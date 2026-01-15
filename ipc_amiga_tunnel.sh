#!/usr/bin/env bash
set -euo pipefail
exec ssh -N -R 9009:/tank/pistorm/ipc/amiga.sock 172.16.0.2