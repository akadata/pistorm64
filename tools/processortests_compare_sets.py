#!/usr/bin/env python3
"""Compare operation coverage between two ProcessorTests directories."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Iterable


def is_dataset_file(path: Path) -> bool:
    return (
        path.suffix == ".json"
        or path.name.endswith(".json.gz")
        or path.name.endswith(".json.bin")
    )


def iter_dataset_files(root: Path) -> Iterable[Path]:
    for path in sorted(root.iterdir()):
        if path.is_file() and is_dataset_file(path):
            yield path


def op_name_from_file(path: Path) -> str:
    name = path.name
    if name.endswith(".json.bin"):
        return name[: -len(".json.bin")]
    if name.endswith(".json.gz"):
        return name[: -len(".json.gz")]
    if name.endswith(".json"):
        return name[: -len(".json")]
    return name


def csv(items: Iterable[str]) -> str:
    values = list(items)
    return ", ".join(values) if values else "(none)"


def parse_args(argv: list[str]) -> argparse.Namespace:
    repo_root = Path(__file__).resolve().parents[1]
    default_root = repo_root / "third_party" / "ProcessorTests"
    parser = argparse.ArgumentParser(
        description="Compare old/new ProcessorTests operation coverage"
    )
    parser.add_argument(
        "--old-dir",
        default=str(default_root / "680x0" / "68000" / "v1"),
    )
    parser.add_argument(
        "--new-dir",
        default=str(default_root / "m68000" / "v1"),
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    old_dir = Path(args.old_dir)
    new_dir = Path(args.new_dir)

    if not old_dir.is_dir():
        print(f"old dataset directory not found: {old_dir}", file=sys.stderr)
        return 2
    if not new_dir.is_dir():
        print(f"new dataset directory not found: {new_dir}", file=sys.stderr)
        return 2

    old_files = list(iter_dataset_files(old_dir))
    new_files = list(iter_dataset_files(new_dir))

    old_ops = {op_name_from_file(p) for p in old_files}
    new_ops = {op_name_from_file(p) for p in new_files}

    matching = sorted(old_ops & new_ops)
    missing_in_new = sorted(old_ops - new_ops)
    missing_in_old = sorted(new_ops - old_ops)

    print(f"OLD dir: {old_dir}")
    print(f"NEW dir: {new_dir}")
    print(f"OLD file count: {len(old_files)}")
    print(f"NEW file count: {len(new_files)}")
    print(f"OLD op count: {len(old_ops)}")
    print(f"NEW op count: {len(new_ops)}")
    print(f"Matching ops ({len(matching)}): {csv(matching)}")
    print(f"Missing in NEW ({len(missing_in_new)}): {csv(missing_in_new)}")
    print(f"Missing in OLD ({len(missing_in_old)}): {csv(missing_in_old)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
