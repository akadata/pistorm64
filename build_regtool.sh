#!/usr/bin/env bash
# Backward-compatible wrapper for build_reg_tools.sh
set -euo pipefail
exec "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/build_reg_tools.sh" "$@"
