"""Decrypt the client's text assets (Lua / XML / CSV).

Container: 8-byte header (FF D9 FF D7 + u16 + u16), then the body XORed with a fixed
16-byte cycle, then a zlib stream.

    plain = zlib.decompress(xor(body, KEY))

Usage:
    python decrypt_assets.py <assets-dir> <out-dir> [--ext lua,xml,csv]
"""
from __future__ import annotations

import argparse
import os
import sys
import zlib

MAGIC = b"\xff\xd9\xff\xd7"
KEY = bytes([0xD6, 0x02, 0x08, 0x00, 0xF4, 0xFE, 0xFF, 0x3F,
             0x01, 0x00, 0x00, 0x00, 0xD0, 0xCA, 0x01, 0x00])


def xor_cyclic(data: bytes, key: bytes) -> bytes:
    n = len(data)
    cycle = (n // len(key)) * key + key[: n % len(key)]
    return bytes(a ^ b for a, b in zip(data, cycle))


def decrypt(raw: bytes) -> bytes:
    return zlib.decompress(xor_cyclic(raw[8:], KEY))


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--ext", default="lua,xml,csv")
    args = ap.parse_args(argv)
    wanted = {"." + e.strip().lower().lstrip(".") for e in args.ext.split(",") if e.strip()}

    ok = fail = skipped = 0
    for root, _dirs, files in os.walk(args.src):
        for name in files:
            if os.path.splitext(name)[1].lower() not in wanted:
                continue
            path = os.path.join(root, name)
            with open(path, "rb") as fh:
                raw = fh.read()
            if raw[:4] != MAGIC:
                skipped += 1
                continue
            rel = os.path.relpath(path, args.src)
            out = os.path.join(args.dst, rel)
            try:
                plain = decrypt(raw)
            except Exception as exc:  # noqa: BLE001
                fail += 1
                print(f"  fail {rel}: {exc}")
                continue
            os.makedirs(os.path.dirname(out), exist_ok=True)
            with open(out, "wb") as fh:
                fh.write(plain)
            ok += 1

    print(f"decrypted={ok} failed={fail} skipped={skipped} -> {args.dst}")
    return 0 if fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
