#!/usr/bin/env bash
set -euo pipefail
MSG="${*:-MARKER}"
echo "$MSG $(date +%s.%N)" | nc <homer_ip_address> 9009