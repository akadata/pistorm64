#!/usr/bin/env python3
"""ProcessorTests runner/validator for PiStorm workflows.

This tool is intentionally lightweight:
- It can generate a reduced subset via ProcessorTests' own subset.py utility.
- It validates JSON, JSON.gz, and JSON.bin test bundles structurally.
- It supports quick and full modes.

It does not execute tests against a CPU core directly; it prepares and validates
datasets so emulator-side runners can consume them predictably.
"""

from __future__ import annotations

import argparse
import gzip
import json
import os
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence


REQUIRED_TEST_KEYS = ("name", "initial", "final", "length", "transactions")


@dataclass
class ValidationSummary:
    files_checked: int = 0
    tests_checked: int = 0
    transactions_checked: int = 0
    errors: int = 0


def eprint(msg: str) -> None:
    print(msg, file=sys.stderr)


def is_supported_test_file(path: Path) -> bool:
    return (
        path.suffix == ".json"
        or path.name.endswith(".json.gz")
        or path.name.endswith(".json.bin")
    )


def canonical_test_filename(path: Path) -> str:
    name = path.name
    if name.endswith(".json.bin"):
        return name[:-4]  # strip trailing ".bin"
    if name.endswith(".json.gz"):
        return name[:-3]  # strip trailing ".gz"
    return name


def test_file_rank(path: Path) -> int:
    # Prefer plain JSON, then gz, then binary-encoded JSON.
    if path.suffix == ".json":
        return 0
    if path.name.endswith(".json.gz"):
        return 1
    if path.name.endswith(".json.bin"):
        return 2
    return 99


def iter_test_files(suite_dir: Path) -> Iterable[Path]:
    selected: dict[str, Path] = {}
    for path in sorted(suite_dir.iterdir()):
        if not path.is_file() or not is_supported_test_file(path):
            continue
        key = canonical_test_filename(path)
        current = selected.get(key)
        if current is None or test_file_rank(path) < test_file_rank(current):
            selected[key] = path
    for key in sorted(selected):
        yield selected[key]

def ensure_json_from_bin(
    path: Path, decode_script: Path, decoded_suite_roots: set[Path]
) -> Path:
    json_path = Path(str(path)[:-4])  # strip ".bin"
    if json_path.exists():
        return json_path

    decode_suite_dir(path.parent, decode_script, decoded_suite_roots)

    if not json_path.exists():
        raise FileNotFoundError(f"decode.py ran but JSON output was not created: {json_path}")
    return json_path


def load_json(path: Path, decode_script: Path, decoded_suite_roots: set[Path]):
    if path.name.endswith(".json.bin"):
        path = ensure_json_from_bin(path, decode_script, decoded_suite_roots)
    if path.name.endswith(".json.gz"):
        with gzip.open(path, "rt", encoding="utf-8") as f:
            return json.load(f)
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def validate_transactions(txns: object, file_path: Path, test_idx: int) -> tuple[int, list[str]]:
    errs: list[str] = []
    count = 0
    allowed_cycle_types = {"r", "w", "t", "n", "re", "we"}
    if not isinstance(txns, list):
        return 0, [f"{file_path}: test[{test_idx}] 'transactions' is not a list"]
    for i, txn in enumerate(txns):
        count += 1
        if not isinstance(txn, list) or len(txn) < 2:
            errs.append(
                f"{file_path}: test[{test_idx}] transaction[{i}] invalid shape "
                f"(expected list len>=2, got {type(txn).__name__})"
            )
            continue
        if not isinstance(txn[0], str):
            errs.append(f"{file_path}: test[{test_idx}] transaction[{i}] cycle type is not a string")
        elif txn[0] not in allowed_cycle_types:
            errs.append(
                f"{file_path}: test[{test_idx}] transaction[{i}] unknown cycle type '{txn[0]}'"
            )
        if not isinstance(txn[1], int):
            errs.append(
                f"{file_path}: test[{test_idx}] transaction[{i}] cycle count is not an int"
            )
        # Non-idle cycles should generally carry bus details.
        if isinstance(txn[0], str) and txn[0] != "n" and len(txn) < 5:
            errs.append(
                f"{file_path}: test[{test_idx}] transaction[{i}] has cycle type '{txn[0]}' "
                f"but is too short (len={len(txn)})"
            )
    return count, errs


def validate_test_object(test_obj: object, file_path: Path, test_idx: int) -> tuple[int, list[str]]:
    errs: list[str] = []
    txn_count = 0

    if not isinstance(test_obj, dict):
        return 0, [f"{file_path}: test[{test_idx}] is not an object"]

    missing = [k for k in REQUIRED_TEST_KEYS if k not in test_obj]
    if missing:
        errs.append(f"{file_path}: test[{test_idx}] missing keys: {', '.join(missing)}")
        return 0, errs

    if not isinstance(test_obj["name"], str):
        errs.append(f"{file_path}: test[{test_idx}] 'name' is not a string")
    if not isinstance(test_obj["initial"], dict):
        errs.append(f"{file_path}: test[{test_idx}] 'initial' is not an object")
    if not isinstance(test_obj["final"], dict):
        errs.append(f"{file_path}: test[{test_idx}] 'final' is not an object")
    if not isinstance(test_obj["length"], int):
        errs.append(f"{file_path}: test[{test_idx}] 'length' is not an int")

    txn_count, txn_errs = validate_transactions(test_obj["transactions"], file_path, test_idx)
    errs.extend(txn_errs)
    return txn_count, errs


def validate_suite(
    suite_dir: Path,
    sample_per_file: int | None,
    decode_script: Path,
    max_error_prints: int = 200,
) -> ValidationSummary:
    summary = ValidationSummary()
    decoded_suite_roots: set[Path] = set()
    printed_errors = 0
    omitted_errors = 0
    files = list(iter_test_files(suite_dir))
    if not files:
        raise FileNotFoundError(
            f"No .json/.json.gz/.json.bin files found in suite dir: {suite_dir}"
        )

    for file_path in files:
        try:
            data = load_json(file_path, decode_script, decoded_suite_roots)
        except Exception as exc:  # noqa: BLE001
            summary.errors += 1
            if printed_errors < max_error_prints:
                eprint(f"{file_path}: failed to load JSON: {exc}")
                printed_errors += 1
            else:
                omitted_errors += 1
            continue

        summary.files_checked += 1
        if not isinstance(data, list):
            summary.errors += 1
            if printed_errors < max_error_prints:
                eprint(f"{file_path}: top-level is not a list")
                printed_errors += 1
            else:
                omitted_errors += 1
            continue

        upper = len(data) if sample_per_file is None else min(len(data), sample_per_file)
        for idx in range(upper):
            txn_count, errs = validate_test_object(data[idx], file_path, idx)
            summary.tests_checked += 1
            summary.transactions_checked += txn_count
            if errs:
                summary.errors += len(errs)
                for err in errs:
                    if printed_errors < max_error_prints:
                        eprint(err)
                        printed_errors += 1
                    else:
                        omitted_errors += 1

    if omitted_errors:
        eprint(f"[processortests] omitted {omitted_errors} additional error lines")

    return summary


def validate_opcode_map(map_file: Path) -> int:
    try:
        with map_file.open("r", encoding="utf-8") as f:
            data = json.load(f)
    except Exception as exc:  # noqa: BLE001
        eprint(f"{map_file}: failed to load opcode map: {exc}")
        return 1

    if not isinstance(data, dict):
        eprint(f"{map_file}: opcode map must be a JSON object")
        return 1
    if len(data) != 65536:
        eprint(f"{map_file}: expected 65536 opcode entries, got {len(data)}")
        return 1

    bad = 0
    for k, v in data.items():
        if not isinstance(k, str):
            bad += 1
        if not isinstance(v, str):
            bad += 1
    if bad:
        eprint(f"{map_file}: found {bad} non-string key/value entries")
        return 1
    return 0


def generate_subset(
    tools_dir: Path,
    source_dir: Path,
    subset_dir: Path,
    subset_percent: float,
) -> None:
    subset_py = tools_dir / "subset.py"
    if not subset_py.exists():
        raise FileNotFoundError(f"subset.py not found: {subset_py}")
    subset_dir.mkdir(parents=True, exist_ok=True)
    cmd = [
        sys.executable,
        str(subset_py),
        str(subset_percent),
        str(source_dir),
        str(subset_dir),
    ]
    subprocess.run(cmd, check=True)


def run_checkdups(tools_dir: Path, suite_dir: Path) -> int:
    checkdups_py = tools_dir / "checkdups.py"
    if not checkdups_py.exists():
        eprint(f"checkdups.py not found: {checkdups_py}")
        return 1
    cmd = [sys.executable, str(checkdups_py), str(suite_dir)]
    return subprocess.run(cmd).returncode


def decode_suite_dir(
    suite_dir: Path, decode_script: Path, decoded_suite_roots: set[Path]
) -> None:
    suite_root = suite_dir.parent
    if suite_root in decoded_suite_roots:
        return
    if not decode_script.exists():
        raise FileNotFoundError(
            f"decode.py not found ({decode_script}) and JSON source missing under {suite_dir}"
        )
    subprocess.run([sys.executable, str(decode_script)], cwd=suite_root, check=True)
    decoded_suite_roots.add(suite_root)


def suite_has_json_payload(suite_dir: Path) -> bool:
    for path in suite_dir.iterdir():
        if path.is_file() and (path.suffix == ".json" or path.name.endswith(".json.gz")):
            return True
    return False


def suite_has_json_bin_payload(suite_dir: Path) -> bool:
    for path in suite_dir.iterdir():
        if path.is_file() and path.name.endswith(".json.bin"):
            return True
    return False


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    repo_root = Path(__file__).resolve().parents[1]
    default_root = repo_root / "third_party" / "ProcessorTests"
    p = argparse.ArgumentParser(description="ProcessorTests dataset runner/validator")
    p.add_argument("--mode", choices=("quick", "full"), default="quick")
    p.add_argument("--source-dir", default=str(default_root / "680x0" / "68000" / "v1"))
    p.add_argument("--suite-dir", default="", help="Explicit suite dir to validate")
    p.add_argument("--tools-dir", default=str(default_root / "tools"))
    p.add_argument("--subset-dir", default="build/processortests/quick")
    p.add_argument("--subset-percent", type=float, default=1.0)
    p.add_argument("--sample-per-file", type=int, default=64)
    p.add_argument("--refresh-subset", action="store_true")
    p.add_argument("--check-dups", action="store_true")
    p.add_argument(
        "--map-file",
        default=str(default_root / "680x0" / "map" / "68000.official.json"),
    )
    p.add_argument(
        "--decode-script",
        default=str(default_root / "680x0" / "68000" / "decode.py"),
        help="Path to decode.py used only when .json.bin exists without matching .json",
    )
    p.add_argument("--check-map", action="store_true")
    return p.parse_args(argv)


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    source_dir = Path(args.source_dir)
    suite_dir = Path(args.suite_dir) if args.suite_dir else None
    tools_dir = Path(args.tools_dir)
    subset_dir = Path(args.subset_dir)
    map_file = Path(args.map_file)
    decode_script = Path(args.decode_script)

    if args.mode == "quick":
        if suite_dir is None:
            suite_dir = subset_dir
        subset_missing = not subset_dir.exists()
        subset_empty = (not subset_missing) and (not any(iter_test_files(subset_dir)))
        if suite_dir == subset_dir and (args.refresh_subset or subset_missing or subset_empty):
            if source_dir.exists() and not suite_has_json_payload(source_dir) and suite_has_json_bin_payload(source_dir):
                # subset.py only understands .json/.json.gz; decode only when required.
                decode_suite_dir(source_dir, decode_script, set())
            print(
                f"[processortests] generating subset: {args.subset_percent:.3f}% "
                f"from {source_dir} -> {subset_dir}"
            )
            generate_subset(tools_dir, source_dir, subset_dir, args.subset_percent)
        sample_per_file: int | None = max(args.sample_per_file, 1)
    else:
        if suite_dir is None:
            suite_dir = source_dir
        sample_per_file = None

    if not suite_dir.exists():
        eprint(f"suite directory not found: {suite_dir}")
        return 2

    if args.check_map:
        map_rc = validate_opcode_map(map_file)
        if map_rc != 0:
            return map_rc
        print(f"[processortests] opcode map OK: {map_file}")

    print(
        f"[processortests] validating suite dir={suite_dir} mode={args.mode} "
        f"sample_per_file={'all' if sample_per_file is None else sample_per_file}"
    )
    summary = validate_suite(suite_dir, sample_per_file, decode_script)
    print(
        f"[processortests] files={summary.files_checked} tests={summary.tests_checked} "
        f"transactions={summary.transactions_checked} errors={summary.errors}"
    )

    if args.check_dups:
        print(f"[processortests] duplicate check: {suite_dir}")
        dup_rc = run_checkdups(tools_dir, suite_dir)
        if dup_rc != 0:
            eprint(f"[processortests] duplicate check failed with rc={dup_rc}")
            return dup_rc

    return 1 if summary.errors else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
