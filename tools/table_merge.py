"""Merge source data tables into the target schema and rebuild the resource pack.

Why: the new engine reads new columns (item Quality/Chip/UnlockType..., monster Hit*...)
and tables the source build never had. Copying old CSVs over the new pack makes the engine
crash while parsing. This tool keeps the new schema, fills it with old rows, and writes the
result back into the pack with the same FF D9 FF D7 envelope the client expects.

    python tools/table_merge.py base.pkg old/assets output.pkg \
        [--overlay-art]
"""
from __future__ import annotations

import argparse
import io
import os
import struct
import sys
import zipfile
import zlib

def _app_bundle(zf: "zipfile.ZipFile") -> str:
    """Locate the app bundle root inside the package."""
    for n in zf.namelist():
        if n.startswith("Payload/") and n.endswith(".app/Info.plist"):
            return n[: -len("Info.plist")]
    raise SystemExit("no app bundle found in package")


def _pack_entry(zf: "zipfile.ZipFile") -> str:
    """Locate the resource pack entry (the archive next to the executable)."""
    app = _app_bundle(zf)
    for n in zf.namelist():
        if n.startswith(app) and n.endswith(".apk"):
            return n
    raise SystemExit("no resource pack found in package")


PACK_ENTRY = None
KEY = bytes([0xD6, 0x02, 0x08, 0x00, 0xF4, 0xFE, 0xFF, 0x3F,
             0x01, 0x00, 0x00, 0x00, 0xD0, 0xCA, 0x01, 0x00])
ART_EXT = (".png", ".ogg", ".omod", ".ent", ".emo", ".oanim", ".otex", ".obj", ".vox")


def confuse(b: bytes) -> bytes:
    return bytes(x ^ KEY[i & 0x0F] for i, x in enumerate(b))


def decrypt_text(raw: bytes) -> str:
    if raw[:4] != b"\xff\xd9\xff\xd7":
        return raw.decode("utf-8", "replace")
    plain = zlib.decompress(confuse(raw[8:]))
    return plain.decode("utf-8", "replace")


def encrypt_text(text: str) -> bytes:
    plain = text.encode("utf-8")
    packed = zlib.compress(plain, 6)
    return b"\xff\xd9\xff\xd7" + struct.pack(">I", len(plain)) + confuse(packed)


OMOD_MAGIC = b"\x89\x67\x45\x23"


def unwrap_asset(raw: bytes) -> bytes:
    """Strip the iOS UnConfuse header envelope (FF D9 FF D8 + N confused bytes)."""
    if raw[:4] == b"\xff\xd9\xff\xd8" and len(raw) >= 8:
        n = int.from_bytes(raw[4:8], "big")
        if 0 < n <= len(raw) - 8:
            return confuse(raw[8:8 + n]) + raw[8 + n:]
    return raw


def png_size(raw: bytes):
    """(width, height) of a PNG, before or after unwrapping the envelope."""
    data = unwrap_asset(raw)
    if data[:8] != b"\x89PNG\r\n\x1a\n" or len(data) < 24:
        return None
    return struct.unpack(">II", data[16:24])



def apply_envelope(new_entry: bytes, raw: bytes) -> bytes:
    """Mirror the envelope the NEW pack uses for the same entry.

    iOS stores .png/.ogg/.omod as: magic FF D9 FF D8 | u32be(N) | confuse(first N) | rest.
    Other types (.ent/.emo/.oanim/.otex/.obj/.vox) are stored raw.
    """
    if raw[:4] == b"\xff\xd9\xff\xd8" or raw[:4] == b"\xff\xd9\xff\xd7":
        return raw  # already wrapped
    if new_entry[:4] == b"\xff\xd9\xff\xd8" and len(new_entry) >= 8:
        n = int.from_bytes(new_entry[4:8], "big")
        if 0 < n <= len(raw):
            return b"\xff\xd9\xff\xd8" + new_entry[4:8] + confuse(raw[:n]) + raw[n:]
    return raw


def wrap_omod(raw: bytes) -> bytes:
    """The iOS client stores .omod inside an UnConfuse envelope: magic + 64 confused bytes."""
    if not raw.startswith(OMOD_MAGIC):
        return raw
    head = 64
    return (b"\xff\xd9\xff\xd8" + len(raw[:head]).to_bytes(4, "big")
            + confuse(raw[:head]) + raw[head:])


def split_csv_line(line: str) -> list[str]:
    out, field, in_quotes = [], [], False
    i = 0
    while i < len(line):
        c = line[i]
        if in_quotes:
            if c == '"':
                if i + 1 < len(line) and line[i + 1] == '"':
                    field.append('"')
                    i += 1
                else:
                    in_quotes = False
            else:
                field.append(c)
        elif c == '"':
            in_quotes = True
        elif c == ",":
            out.append("".join(field))
            field = []
        elif c != "\r":
            field.append(c)
        i += 1
    out.append("".join(field))
    return out


def join_csv_field(value: str) -> str:
    if any(ch in value for ch in (",", '"', "\n")):
        return '"' + value.replace('"', '""') + '"'
    return value


def parse_table(text: str):
    lines = [ln for ln in text.replace("\r\n", "\n").split("\n") if ln]
    if len(lines) < 3:
        return None
    return {
        "header_cn": split_csv_line(lines[0]),
        "header_en": split_csv_line(lines[1]),
        "rows": [split_csv_line(ln) for ln in lines[2:]],
    }


# Columns whose values reference packaged assets (textures, sounds, models...).
# Those must come from the target package: its assets are what the runtime actually ships,
# id/name can resolve to nothing and the engine dereferences NULL while building materials.
ASSET_COLUMN_HINTS = ("tex", "icon", "sound", "model", "img", "image", "effect",
                      "particle", "anim", "shader", "material", "wield", "place",
                      "drop", "iconpath")

# Columns that pick an engine behaviour class (shader variant). A value from the other
# version can leave the engine without a matching compiled shader, which aborts with
# "Cannot find opengl shader: %s", so these always keep the NEW client's value.
ENGINE_BOUND_COLUMNS = ("type",)


def is_asset_column(name: str) -> bool:
    low = name.lower()
    if low in ENGINE_BOUND_COLUMNS:
        return True
    return any(h in low for h in ASSET_COLUMN_HINTS)


def row_key(row: list[str]) -> str:
    return row[0].strip() if row else ""


def merge_table(new_text: str, old_text: str) -> tuple[str, int, int]:
    new = parse_table(new_text)
    old = parse_table(old_text)
    if not new or not old:
        return new_text, 0, 0

    cols = new["header_en"]
    col_index = {name: i for i, name in enumerate(cols)}
    old_cols = {name: i for i, name in enumerate(old["header_en"])}

    new_by_key = {row_key(r): r for r in new["rows"]}
    merged = []
    for old_row in old["rows"]:
        key = row_key(old_row)
        if not key:
            continue
        new_row = new_by_key.get(key, [])
        out = []
        for name in cols:
            if name and not is_asset_column(name) and name in old_cols and old_cols[name] < len(old_row):
                out.append(old_row[old_cols[name]])
            elif col_index[name] < len(new_row):
                out.append(new_row[col_index[name]])
            else:
                out.append("")
        merged.append(out)

    # keep new-only rows so the engine still has its required entries
    old_keys = {row_key(r) for r in old["rows"]}
    for new_row in new["rows"]:
        if row_key(new_row) and row_key(new_row) not in old_keys:
            merged.append(new_row)

    lines = [",".join(join_csv_field(v) for v in new["header_cn"]),
             ",".join(join_csv_field(v) for v in new["header_en"])]
    for row in merged:
        lines.append(",".join(join_csv_field(v) for v in row))
    return "\n".join(lines) + "\n", len(old["rows"]), len(merged)


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("ipa", help="base package")
    ap.add_argument("assets", help="source asset tree")
    ap.add_argument("out")
    ap.add_argument("--size-match", action="store_true",
                    help="only overlay png whose dimensions match the new pack")
    ap.add_argument("--art-ext", default=None,
                    help="comma list of art extensions to overlay, e.g. png,ogg")
    ap.add_argument("--overlay-art", action="store_true",
                    help="also take textures/models/worlds from the source tree")
    args = ap.parse_args(argv)

    with zipfile.ZipFile(args.ipa) as zin:
        pack_name = _pack_entry(zin)
        pack_raw = zin.read(pack_name)
        new_pack = zipfile.ZipFile(io.BytesIO(pack_raw))
        entries = {n: new_pack.read(n) for n in new_pack.namelist() if not n.endswith("/")}

    # rebuild the CSV entries from the merged schema
    merged_count = replaced = 0
    for name in sorted(entries):
        if not name.endswith(".csv"):
            continue
        rel = name[len("assets/"):] if name.startswith("assets/") else name
        old_path = os.path.join(args.assets, *rel.split("/"))
        if not os.path.isfile(old_path):
            continue
        new_text = decrypt_text(entries[name])
        old_text = decrypt_text(open(old_path, "rb").read())
        merged_text, old_rows, out_rows = merge_table(new_text, old_text)
        entries[name] = encrypt_text(merged_text)
        merged_count += 1
        replaced += old_rows
        if merged_count <= 8:
            print(f"  {rel:<28} old={old_rows:<5} merged={out_rows}")
    print(f"CSV merged: {merged_count} tables, {replaced} old rows folded into the new schema")

    if args.overlay_art:
        wanted_ext = tuple("." + e.strip().lstrip(".").lower()
                           for e in args.art_ext.split(",")) if args.art_ext else ART_EXT
        art = 0
        for dirpath, _dirs, files in os.walk(args.assets):
            for f in files:
                if not f.lower().endswith(wanted_ext):
                    continue
                if f.lower().startswith("shadercache"):
                    continue  # compiled per engine version: never copy across versions
                full = os.path.join(dirpath, f)
                rel = os.path.relpath(full, args.assets).replace("\\", "/")
                key = "assets/" + rel
                if key in entries:
                    raw = open(full, "rb").read()
                    if args.size_match and key.endswith(".png"):
                        if png_size(raw) != png_size(entries[key]):
                            continue
                    entries[key] = apply_envelope(entries[key], raw)
                    art += 1
        print(f"art overlaid: {art} files")

    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as zout:
        for name in sorted(entries):
            zout.writestr(name, entries[name])
    pack = buf.getvalue()
    print(f"data pack rebuilt: {len(pack) / 1048576:.1f} MB, {len(entries)} entries")

    with zipfile.ZipFile(args.ipa) as zin, zipfile.ZipFile(args.out, "w", zipfile.ZIP_DEFLATED,
                                                          compresslevel=6) as zout:
        for item in zin.infolist():
            if item.filename == pack_name:
                zout.writestr(item, pack)
            elif item.filename.endswith("/"):
                zout.writestr(item, b"")
            else:
                zout.writestr(item, zin.read(item.filename))
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
