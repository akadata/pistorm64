#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: run_musashi_ref_tests.sh --driver <path> --tests-root <path> [options]

Options:
  --mode quick|68040|68000|all       Test set selector (default: quick)
  --cpu 68000|68010|68020|68030|68040 CPU type for driver (default: 68040)
  --iterations N                      Driver execute loop count (default: 100)
  --cycles N                          Cycles per execute loop (default: 0x1000000)
  --timeout-sec N                     Per-test timeout in seconds (default: 20)
  --xfail-file <path>                 Optional list of expected-failing test paths
  --require-empty-xfail               Fail if xfail file has any active entries
  --allow-xpass                       Do not fail run when an expected failure passes
EOF
}

driver=""
tests_root=""
mode="quick"
cpu="68040"
iterations="100"
cycles="0x1000000"
timeout_sec="20"
xfail_file=""
require_empty_xfail=0
allow_xpass=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --driver) driver="$2"; shift 2 ;;
        --tests-root) tests_root="$2"; shift 2 ;;
        --mode) mode="$2"; shift 2 ;;
        --cpu) cpu="$2"; shift 2 ;;
        --iterations) iterations="$2"; shift 2 ;;
        --cycles) cycles="$2"; shift 2 ;;
        --timeout-sec) timeout_sec="$2"; shift 2 ;;
        --xfail-file) xfail_file="$2"; shift 2 ;;
        --require-empty-xfail) require_empty_xfail=1; shift 1 ;;
        --allow-xpass) allow_xpass=1; shift 1 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage; exit 2 ;;
    esac
done

if [[ -z "$driver" || -z "$tests_root" ]]; then
    usage
    exit 2
fi

if [[ ! -x "$driver" ]]; then
    echo "Driver is not executable: $driver" >&2
    exit 2
fi

if [[ ! -d "$tests_root" ]]; then
    echo "Tests root missing: $tests_root" >&2
    exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ -z "$xfail_file" ]]; then
    case "${mode}:${cpu}" in
        68000:68000)
            candidate="${script_dir}/baselines/musashi_ref_68000.xfail"
            if [[ -f "$candidate" ]]; then
                xfail_file="$candidate"
            fi
            ;;
    esac
fi

declare -A xfails=()
if [[ -n "$xfail_file" ]]; then
    if [[ ! -f "$xfail_file" ]]; then
        echo "xfail file not found: $xfail_file" >&2
        exit 2
    fi
    while IFS= read -r line || [[ -n "$line" ]]; do
        line="${line%%#*}"
        line="${line#"${line%%[![:space:]]*}"}"
        line="${line%"${line##*[![:space:]]}"}"
        [[ -z "$line" ]] && continue
        xfails["$line"]=1
    done < "$xfail_file"
fi

if [[ $require_empty_xfail -eq 1 && ${#xfails[@]} -ne 0 ]]; then
    echo "CI policy violation: xfail baseline is not empty (${#xfails[@]} active entries)." >&2
    echo "Remove xfail entries (or fix tests) before using --require-empty-xfail." >&2
    exit 1
fi

declare -a tests=()
case "$mode" in
    quick)
        tests=(
            "mc68000/add.bin"
            "mc68000/move.bin"
            "mc68040/jmp.bin"
            "mc68040/trapcc.bin"
        )
        ;;
    68040)
        mapfile -t tests < <(cd "$tests_root" && ls -1 mc68040/*.bin | sort)
        ;;
    68000)
        mapfile -t tests < <(cd "$tests_root" && ls -1 mc68000/*.bin | sort)
        ;;
    all)
        mapfile -t tests < <(cd "$tests_root" && ls -1 mc68000/*.bin mc68040/*.bin | sort)
        ;;
    *)
        echo "Unsupported mode: $mode" >&2
        exit 2
        ;;
esac

if [[ ${#tests[@]} -eq 0 ]]; then
    echo "No tests selected for mode=$mode" >&2
    exit 2
fi

pass=0
fail=0
xpass=0
xfail=0
miss=0
for rel in "${tests[@]}"; do
    bin="$tests_root/$rel"
    expected_fail=0
    if [[ -n "${xfails[$rel]+x}" ]]; then
        expected_fail=1
    fi

    if [[ ! -f "$bin" ]]; then
        echo "MISS $rel"
        miss=$((miss + 1))
        fail=$((fail + 1))
        continue
    fi

    if timeout "${timeout_sec}s" "$driver" "$bin" --cpu "$cpu" --iterations "$iterations" --cycles "$cycles" >/tmp/musashi_ref_test.out 2>&1; then
        if [[ $expected_fail -eq 1 ]]; then
            echo "XPASS $rel"
            xpass=$((xpass + 1))
        else
            echo "PASS $rel"
            pass=$((pass + 1))
        fi
    else
        if [[ $expected_fail -eq 1 ]]; then
            echo "XFAIL $rel"
            xfail=$((xfail + 1))
        else
            echo "FAIL $rel"
            sed -n '1,40p' /tmp/musashi_ref_test.out
            fail=$((fail + 1))
        fi
    fi
done

echo "SUMMARY mode=$mode pass=$pass fail=$fail xfail=$xfail xpass=$xpass miss=$miss total=${#tests[@]}"
if [[ $fail -ne 0 ]]; then
    exit 1
fi
if [[ $xpass -ne 0 && $allow_xpass -eq 0 ]]; then
    echo "Unexpected passes detected. Update xfail baseline if this is intended." >&2
    exit 1
fi
