#!/usr/bin/env python3
"""Make the last press replace the captured first time in an isolated copy."""

import argparse
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("isolated_owner", type=Path)
    args = parser.parse_args()
    target = args.isolated_owner.resolve()
    if target == ROOT or ROOT.is_relative_to(target) or target.is_relative_to(ROOT):
        parser.error("refusing to modify the operator's owner checkout")
    path = target / "src/runtime_combo/runtime_combo.c"
    if not path.resolve().is_relative_to(target):
        parser.error("refusing a source link outside the disposable copy")
    source = path.read_text()
    original = "const int64_t timestamp = pending_keys[0].timestamp;"
    if source.count(original) != 2:
        parser.error("expected both captured-first activation sites")
    path.write_text(
        source.replace(
            original,
            "const int64_t timestamp = pending_keys[pending_count - 1].timestamp;",
        )
    )
    print("Applied active captured-time fault")


if __name__ == "__main__":
    main()
