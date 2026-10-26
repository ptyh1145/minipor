"""Normalise a PNG's chunk structure to match a reference file.

The client ships palette PNGs whose chunk layout is not uniform across versions
(some have tRNS, some do not). The engine's own decoder is sensitive to this, so an
old file dropped in unchanged can corrupt the whole texture set.

Rule: keep the old image data (IHDR/PLTE/IDAT) but make the auxiliary chunks match the
reference — add the reference tRNS when the old one lacks it, drop it when the reference
lacks it.

    from png_normalize import normalise
    fixed = normalise(old_bytes, reference_bytes)
"""
from __future__ import annotations

import struct
import zlib

SIG = b"\x89PNG\r\n\x1a\n"


def chunks(png: bytes):
    if png[:8] != SIG:
        return []
    out = []
    i = 8
    while i + 8 <= len(png):
        ln = struct.unpack(">I", png[i:i + 4])[0]
        typ = png[i + 4:i + 8]
        data = png[i + 8:i + 8 + ln]
        crc = png[i + 8 + ln:i + 12 + ln]
        if len(crc) != 4:
            break
        out.append((typ, data, crc))
        if typ == b"IEND":
            break
        i += 12 + ln
    return out


def build(chs) -> bytes:
    buf = bytearray(SIG)
    for typ, data, crc in chs:
        buf += struct.pack(">I", len(data)) + typ + data + crc
    return bytes(buf)


def normalise(old: bytes, reference: bytes) -> bytes:
    old_ch = chunks(old)
    ref_ch = chunks(reference)
    if not old_ch or not ref_ch:
        return old

    ref_trns = next((c for c in ref_ch if c[0] == b"tRNS"), None)
    old_has = any(c[0] == b"tRNS" for c in old_ch)

    out = []
    inserted = False
    for typ, data, crc in old_ch:
        if typ == b"tRNS":
            continue  # re-added below only when the reference has one
        out.append((typ, data, crc))
        if typ == b"PLTE" and not inserted:
            if ref_trns is not None:
                out.append(ref_trns)
                inserted = True
            else:
                inserted = True

    return build(out)


def verify(path: bytes) -> bool:
    """True when every chunk CRC is intact."""
    for typ, data, crc in chunks(path):
        if zlib.crc32(typ + data) & 0xFFFFFFFF != struct.unpack(">I", crc)[0]:
            return False
    return True
