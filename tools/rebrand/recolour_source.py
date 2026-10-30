#!/usr/bin/env python3
"""Rewrite every 0xAARRGGBB colour literal in the editor code through the
РИЛАЙ palette (tools/rebrand/palette.py), the same transform the skins get,
so code-drawn pointers, glows and meters match the recoloured artwork.

Not idempotent: run once on the pre-rebrand sources.
Usage: python3 tools/rebrand/recolour_source.py [--dry-run]
"""
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
from palette import recolour_hex  # noqa: E402

FILES = sorted(glob.glob(os.path.join(ROOT, "shared", "ui", "*.cpp")) +
               glob.glob(os.path.join(ROOT, "shared", "ui", "*.h")) +
               glob.glob(os.path.join(ROOT, "plugins", "*", "PluginEditor.cpp")) +
               glob.glob(os.path.join(ROOT, "plugins", "*", "PluginEditor.h")))
LIT = re.compile(r"0x([0-9a-fA-F]{8})\b")


def main():
    dry = "--dry-run" in sys.argv
    total = 0
    for f in FILES:
        src = open(f, encoding="utf-8").read()
        count = [0]

        def sub(m):
            v = int(m.group(1), 16)
            n = recolour_hex(v)
            if n != v:
                count[0] += 1
            return "0x%08x" % n

        out = LIT.sub(sub, src)
        if count[0]:
            total += count[0]
            print(os.path.relpath(f, ROOT), count[0])
            if not dry:
                open(f, "w", encoding="utf-8").write(out)
    print("changed", total)


if __name__ == "__main__":
    main()
