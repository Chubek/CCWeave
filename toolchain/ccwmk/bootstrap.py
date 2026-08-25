#!/usr/bin/env python3
"""Bootstrap CCWmk from the local Weavefile subset.

This script intentionally uses plain GNU Make compatibility only.
"""

from __future__ import annotations

import pathlib
import subprocess
import sys


def main() -> int:
    root = pathlib.Path(__file__).resolve().parent
    cmd = ["make", "-f", "Weavefile"]
    return subprocess.call(cmd, cwd=root)


if __name__ == "__main__":
    raise SystemExit(main())

