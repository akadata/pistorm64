#!/usr/bin/env bash
set -euo pipefail

CPU="${1:-020}"
make CPU="$CPU"
