#!/usr/bin/env python3
"""Verify generated/read-back storage contains exactly the intended photo assets."""
from __future__ import annotations

import argparse
import hashlib
import pathlib

from PIL import Image


def digest(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=pathlib.Path, required=True)
    parser.add_argument("--extracted", type=pathlib.Path, required=True)
    args = parser.parse_args()
    extracted = {p.name.lower(): p for p in args.extracted.iterdir() if p.is_file()}
    expected = sorted(p.name.lower() for p in args.source.iterdir() if p.is_file())
    if sorted(extracted) != sorted(expected):
        raise RuntimeError(f"storage files differ: {sorted(extracted)}")
    for name in expected:
        source = args.source / name
        target = extracted[name]
        if digest(source) != digest(target):
            raise RuntimeError(f"hash mismatch: {name}")
        if name.endswith(".jpg") and Image.open(target).size != (536, 536):
            raise RuntimeError(f"wrong dimensions: {name}")
    photo_count = sum(name.endswith(".jpg") for name in expected)
    print(f"storage assets: {photo_count}/{photo_count} photos + manifest + glyph, hashes match")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
