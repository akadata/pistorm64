#!/bin/sh
set -eu

target="${1:-unknown}"
rev="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
dirty=""
if git diff --quiet --ignore-submodules -- 2>/dev/null; then
  dirty=""
else
  dirty="-dirty"
fi
date_utc="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

cat > BUILD_VERSION.txt <<EOF
target=$target
git_rev=${rev}${dirty}
build_utc=$date_utc
EOF
