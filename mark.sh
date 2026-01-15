#!/usr/bin/env bash
# Script to send markers from amiga to homer
msg="${*:-MARK}"
echo "$msg $(date +%s.%N)" | nc 172.16.0.2 9009