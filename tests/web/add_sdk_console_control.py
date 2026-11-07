"""Add one dormant console site to an isolated installed SDK for its AST audit."""

import argparse
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("owner", type=Path)
    args = parser.parse_args()
    root = args.owner.resolve(strict=True)
    actual_owner = Path(__file__).resolve().parents[2]
    if root.is_relative_to(actual_owner):
        raise SystemExit("control requires a distinct disposable owner copy")
    target = (
        root / "web/node_modules/@cormoran/zmk-studio-react-hook/lib/useZMKApp.js"
    ).resolve(strict=True)
    if not target.is_relative_to(root):
        raise SystemExit("control SDK must be inside its isolated owner")
    source = target.read_text()
    marker = "function ownerDormantConsoleControl()"
    if marker in source:
        raise SystemExit("control site already exists")
    target.write_text(
        source
        + '\nfunction ownerDormantConsoleControl() { console.warn("synthetic-console-control"); }\n'
    )
    print("Added one dormant SDK console site for the exact reviewed-sites assertion")


if __name__ == "__main__":
    main()
