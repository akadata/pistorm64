#!/usr/bin/env python3
import argparse
import csv
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time

def compile_regexes(patterns):
    return [(pat, re.compile(pat)) for pat in patterns]


def read_new_lines(path, fh_pos):
    try:
        with open(path, "r", errors="replace") as f:
            f.seek(fh_pos)
            data = f.read()
            fh_pos = f.tell()
    except FileNotFoundError:
        return fh_pos, []
    if not data:
        return fh_pos, []
    return fh_pos, data.splitlines()


def replace_loopcycles(lines, loopcycles):
    out = []
    found = False
    for line in lines:
        if line.strip().lower().startswith("loopcycles"):
            out.append(f"loopcycles {loopcycles}\n")
            found = True
        else:
            out.append(line)
    if not found:
        out.append(f"loopcycles {loopcycles}\n")
    return out


def strip_piscsi(lines):
    out = []
    for line in lines:
        stripped = line.strip().lower()
        if stripped.startswith("setvar piscsi"):
            continue
        out.append(line)
    return out


def run_one(args, loopcycles, fail_res, progress_res, require_res):
    with open(args.config, "r", errors="replace") as f:
        base_lines = f.readlines()

    lines = replace_loopcycles(base_lines, loopcycles)
    if args.strip_piscsi:
        lines = strip_piscsi(lines)

    with tempfile.TemporaryDirectory() as td:
        cfg_path = os.path.join(td, "config.cfg")
        log_path = os.path.join(td, "run.log")
        stdout_path = os.path.join(td, "stdout.log")

        with open(cfg_path, "w") as f:
            f.writelines(lines)

        cmd = [args.emulator, "--config", cfg_path, "--log", log_path]
        if args.log_level:
            cmd += ["--log-level", args.log_level]
        if args.extra_args:
            cmd += args.extra_args
        if args.sudo:
            sudo_cmd = ["sudo"]
            if args.sudo_noprompt:
                sudo_cmd.append("-n")
            cmd = sudo_cmd + cmd

        start = time.monotonic()
        deadline = start + args.timeout
        progress_deadline = start + args.progress_timeout
        progress_seen = False
        progress_time = None
        require_seen = False
        require_time = None
        stall_flag = False
        first_fail = ""
        last_line = ""
        log_pos = 0

        proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )

        with open(stdout_path, "w") as stdout_f:
            while True:
                now = time.monotonic()

                # read stdout lines if available
                if proc.stdout is not None:
                    line = proc.stdout.readline()
                    if line:
                        stdout_f.write(line)
                        stdout_f.flush()
                        last_line = line.strip() or last_line
                        for name, rgx in fail_res:
                            if rgx.search(line):
                                first_fail = name
                                break
                        if not progress_seen:
                            for _, rgx in progress_res:
                                if rgx.search(line):
                                    progress_seen = True
                                    progress_time = now - start
                                    break
                        if require_res and not require_seen:
                            for _, rgx in require_res:
                                if rgx.search(line):
                                    require_seen = True
                                    require_time = now - start
                                    break

                # read new log lines
                log_pos, new_lines = read_new_lines(log_path, log_pos)
                for line in new_lines:
                    last_line = line.strip() or last_line
                    for name, rgx in fail_res:
                        if rgx.search(line):
                            first_fail = name
                            break
                    if not progress_seen:
                        for _, rgx in progress_res:
                            if rgx.search(line):
                                progress_seen = True
                                progress_time = now - start
                                break
                    if require_res and not require_seen:
                        for _, rgx in require_res:
                            if rgx.search(line):
                                require_seen = True
                                require_time = now - start
                                break

                if first_fail:
                    result = "FAIL"
                    break

                if now >= deadline:
                    result = "PASS" if progress_seen else "STALL"
                    if not progress_seen:
                        stall_flag = True
                    break

                if now >= progress_deadline and not progress_seen:
                    stall_flag = True

                if proc.poll() is not None:
                    code = proc.returncode
                    if code == 0 and not first_fail:
                        result = "FAIL"
                        first_fail = "exited_early"
                    else:
                        result = "FAIL"
                        if not first_fail:
                            first_fail = f"exit_{code}"
                    break

                time.sleep(0.01)

        # stop process if still running
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                proc.kill()

        seconds = time.monotonic() - start
        if stall_flag and result == "PASS":
            result = "STALL"

        if result in ("PASS", "STALL") and require_res:
            if not require_seen:
                result = "FAIL"
                first_fail = "required_marker_missing"
            elif args.require_marker_before > 0 and require_time is not None and require_time > args.require_marker_before:
                result = "FAIL"
                first_fail = f"required_marker_late_{require_time:.2f}s"

        # if fail and we never captured a line, try to grab last log line
        if not last_line:
            try:
                with open(log_path, "r", errors="replace") as f:
                    for line in f:
                        if line.strip():
                            last_line = line.strip()
            except FileNotFoundError:
                pass

        if result == "FAIL" and first_fail.startswith("exit_"):
            combined = ""
            try:
                with open(stdout_path, "r", errors="replace") as f:
                    combined += f.read()
            except FileNotFoundError:
                pass
            try:
                with open(log_path, "r", errors="replace") as f:
                    combined += f.read()
            except FileNotFoundError:
                pass
            if re.search(r"/dev/mem|/dev/vcio|Permission denied|must be root", combined, re.I):
                first_fail = "needs_root"

        # save logs if requested
        if args.keep_logs:
            out_dir = os.path.join(args.keep_logs, f"loopcycles_{loopcycles}")
            os.makedirs(out_dir, exist_ok=True)
            shutil.copy(cfg_path, os.path.join(out_dir, "config.cfg"))
            if os.path.exists(log_path):
                shutil.copy(log_path, os.path.join(out_dir, "run.log"))
            if os.path.exists(stdout_path):
                shutil.copy(stdout_path, os.path.join(out_dir, "stdout.log"))

        return {
            "loopcycles": loopcycles,
            "result": result,
            "seconds": f"{seconds:.2f}",
            "first_fail_match": first_fail,
            "last_log_line": last_line,
        }


def main():
    parser = argparse.ArgumentParser(description="Sweep loopcycles and classify PASS/FAIL/STALL.")
    parser.add_argument("--emulator", default="./emulator", help="Emulator binary path")
    parser.add_argument("--config", required=True, help="Base config file")
    parser.add_argument("--log-level", default="debug", help="Log level for emulator")
    parser.add_argument("--min", dest="min_cycles", type=int, required=True)
    parser.add_argument("--max", dest="max_cycles", type=int, required=True)
    parser.add_argument("--step", type=int, required=True)
    parser.add_argument("--timeout", type=float, default=30.0, help="Seconds per run")
    parser.add_argument("--progress-timeout", type=float, default=10.0, help="Seconds to see progress")
    parser.add_argument("--strip-piscsi", action="store_true", help="Strip setvar piscsi* lines")
    parser.add_argument("--sudo", action="store_true", help="Run emulator via sudo")
    parser.add_argument("--sudo-noprompt", action="store_true", help="Pass -n to sudo (no prompt)")
    parser.add_argument("--fail-pattern", action="append", default=[])
    parser.add_argument("--progress-marker", action="append", default=[])
    parser.add_argument("--require-marker", action="append", default=[])
    parser.add_argument("--require-marker-before", type=float, default=0.0, help="Require marker within N seconds")
    parser.add_argument("--keep-logs", default="", help="Directory to keep per-run logs")
    parser.add_argument("--extra-arg", action="append", default=[], dest="extra_args")

    args = parser.parse_args()

    if not args.fail_pattern:
        args.fail_pattern = [
            r"PRIVILEGE VIOLATION pc=0x00F80BE2",
            r"unhandled PFLUSHA",
            r"Segmentation fault",
            r"Aborted",
            r"Received sig",
            r"Guru",
        ]

    if not args.progress_marker:
        args.progress_marker = [
            r"Kickstart",
            r"IPL thread created successfully",
            r"OVL:0",
            r"\[INFO\] \[AUTOCONF\]",
        ]

    fail_res = compile_regexes(args.fail_pattern)
    progress_res = compile_regexes(args.progress_marker)
    require_res = compile_regexes(args.require_marker)

    results = []
    for lc in range(args.min_cycles, args.max_cycles + 1, args.step):
        res = run_one(args, lc, fail_res, progress_res, require_res)
        results.append(res)
        print(f"loopcycles={lc} result={res['result']} seconds={res['seconds']} fail={res['first_fail_match']}")

    with open("results.csv", "w", newline="") as f:
        writer = csv.DictWriter(
            f,
            fieldnames=["loopcycles", "result", "seconds", "first_fail_match", "last_log_line"],
        )
        writer.writeheader()
        writer.writerows(results)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
