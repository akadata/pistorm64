#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

gcc -O2 -Wall -Wextra -I. -Iinclude -Iinclude/uapi tools/pistorm_truth_test.c -o pistorm_truth_test
echo "Built ./pistorm_truth_test"
