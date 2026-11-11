#!/usr/bin/env python3
"""Prepare the mutable-tail fault in a distinct disposable container copy."""

import argparse
from pathlib import Path
import re


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
    first = source.index("static void release_pending_keys(void) {")
    last = source.index("\nstatic void cleanup_pending(void)", first)
    function = source[first:last]
    snapshot = """struct runtime_combo_pending_key
        replay[CONFIG_ZMK_RUNTIME_COMBO_MAX_POSITIONS_PER_COMBO];
    memcpy(replay, pending_keys, count * sizeof(replay[0]));"""
    tokens = re.findall(r"[A-Za-z_]\w*|\d+|\S", snapshot)
    pattern = r"\s*".join(re.escape(token) for token in tokens)
    if len(list(re.finditer(pattern, function))) != 1:
        parser.error("expected exactly one immutable local replay snapshot")
    fault = re.sub(
        pattern,
        "struct runtime_combo_pending_key *replay = pending_keys;",
        function,
        count=1,
    )
    path.write_text(source[:first] + fault + source[last:])
    print("Applied mutable replay-tail fault")


if __name__ == "__main__":
    main()
