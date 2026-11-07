"""Reject actual sanitizer report syntax even when recovery leaves exit status zero."""

from pathlib import Path
import re


def require_clean_sanitizers(raw: str, context: str):
    if re.search(
        r"(?:ERROR|WARNING|SUMMARY): (?:Address|Leak|Memory|Thread|UndefinedBehavior)Sanitizer:"
        r"|(?:Address|UndefinedBehavior)Sanitizer:(?:DEADLYSIGNAL| CHECK failed:)"
        r"|[^\r\n]+:\d+(?::\d+)?: runtime error:",
        raw,
    ):
        raise AssertionError(f"{context}: sanitizer diagnostic")


if __name__ == "__main__":
    import sys

    for name in sys.argv[1:]:
        require_clean_sanitizers(Path(name).read_text(), name)
