#!/usr/bin/env bash
set -euo pipefail

# Comprehensive test script for PiStorm GPCLK frequencies on Pi5
# Tests different clock frequencies to determine optimal operation

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
  cat <<'EOF'
Usage:
  ./tools/test_gpclk_frequencies.sh [--start-freq Hz] [--end-freq Hz] [--step-freq Hz]

Examples:
  ./tools/test_gpclk_frequencies.sh              # Test 50MHz to 200MHz in 25MHz steps
  ./tools/test_gpclk_frequencies.sh --start-freq 100000000 --end-freq 200000000 --step-freq 10000000

Notes:
  - Tests each frequency with a brief bus probe
  - Records whether the CPLD responds at each frequency
  - Requires physical connection to CPLD/Amiga
EOF
}

start_freq=50000000
end_freq=200000000
step_freq=25000000

while [[ $# -gt 0 ]]; do
  case "$1" in
    --start-freq)
      start_freq="$2"
      shift 2
      ;;
    --end-freq)
      end_freq="$2"
      shift 2
      ;;
    --step-freq)
      step_freq="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

echo "[freq-test] Testing GPCLK frequencies from $start_freq Hz to $end_freq Hz in $step_freq Hz steps"
echo "[freq-test] Repository root: $REPO_ROOT"

# Function to test a specific frequency
test_frequency() {
  local freq=$1
  local freq_mhz=$(echo "$freq 1000000" | awk '{printf "%.1f", $1/$2}')
  
  echo ""
  echo "[freq-test] Testing $freq_mhz MHz ($freq Hz)..."
  
  # Remove previous setup
  sudo dtoverlay -r pistorm-gpclk0-pll5ff 2>/dev/null || true
  sudo dtoverlay -r pistorm-gpclk0 2>/dev/null || true
  sudo rmmod pistorm_gpclk0 2>/dev/null || true
  
  # Load overlay with specific frequency
  echo "[freq-test] Loading overlay with $freq Hz..."
  if ! sudo dtoverlay -d "$REPO_ROOT/pi5/gpclk0" pistorm-gpclk0-pll5ff "freq=$freq" 2>/dev/null; then
    echo "[freq-test] FAILED: Could not load overlay for $freq_mhz MHz"
    return 1
  fi
  
  # Load kernel module
  if ! sudo insmod "$REPO_ROOT/pi5/gpclk0/pistorm_gpclk0.ko" 2>/dev/null; then
    echo "[freq-test] FAILED: Could not load kernel module for $freq_mhz MHz"
    sudo dtoverlay -r pistorm-gpclk0-pll5ff 2>/dev/null || true
    return 1
  fi
  
  # Verify clock is running
  sleep 0.5
  if [[ -d "/sys/kernel/debug/clk/clk_gp0" ]]; then
    actual_rate=$(cat /sys/kernel/debug/clk/clk_gp0/clk_rate 2>/dev/null || echo "0")
    echo "[freq-test] Requested: $freq_mhz MHz, Actual: $(echo "$actual_rate 1000000" | awk '{printf "%.1f", $1/$2}') MHz"
  fi
  
  # Test with emulator (no actual transactions, just clock presence)
  echo "[freq-test] Testing clock transitions..."
  result=$(sudo env PISTORM_ENABLE_GPCLK=0 PISTORM_RP1_LEAVE_CLK_PIN=1 "$REPO_ROOT/emulator" --gpclk-probe 2>/dev/null | grep "clk_transitions" || true)
  if [[ -n "$result" ]]; then
    echo "[freq-test] Clock transitions detected: $result"
  else
    echo "[freq-test] No clock transitions detected"
  fi
  
  # Brief bus probe to see if CPLD responds
  echo "[freq-test] Testing CPLD response..."
  bus_result=$(timeout 5 sudo env PISTORM_ENABLE_GPCLK=0 PISTORM_RP1_LEAVE_CLK_PIN=1 PISTORM_TXN_TIMEOUT_US=100000 "$REPO_ROOT/emulator" --bus-probe 2>/dev/null | grep -E "(status|vector|PC|SP|invalid)" || true)
  
  if [[ -n "$bus_result" ]]; then
    echo "[freq-test] CPLD RESPONSE DETECTED:"
    echo "$bus_result" | head -n 5
    echo "[freq-test] RESULT: SUCCESS at $freq_mhz MHz"
    echo "$freq_mhz MHz: SUCCESS - CPLD responded" >> "$REPO_ROOT/freq_test_results.txt"
  else
    echo "[freq-test] No CPLD response at $freq_mhz MHz"
    echo "$freq_mhz MHz: NO RESPONSE" >> "$REPO_ROOT/freq_test_results.txt"
  fi
  
  # Clean up for next test
  sudo dtoverlay -r pistorm-gpclk0-pll5ff 2>/dev/null || true
  sudo rmmod pistorm_gpclk0 2>/dev/null || true
  
  return 0
}

# Create results file
echo "[freq-test] Starting frequency sweep test..." > "$REPO_ROOT/freq_test_results.txt"
echo "[freq-test] Results will be logged to $REPO_ROOT/freq_test_results.txt"

# Test each frequency
current_freq=$start_freq
while [[ $current_freq -le $end_freq ]]; do
  test_frequency $current_freq
  current_freq=$((current_freq + step_freq))
  sleep 1  # Brief pause between tests
done

echo ""
echo "[freq-test] Frequency sweep complete!"
echo "[freq-test] Results summary:"
cat "$REPO_ROOT/freq_test_results.txt"

echo ""
echo "[freq-test] To run with the best frequency found:"
echo "[freq-test] 1. Install: ./tools/install_pistorm_gpclk.sh [FREQ]"
echo "[freq-test] 2. Run: sudo env PISTORM_ENABLE_GPCLK=0 PISTORM_RP1_LEAVE_CLK_PIN=1 PISTORM_TXN_TIMEOUT_US=200000 ./emulator --config basic.cfg"