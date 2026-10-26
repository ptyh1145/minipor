"""Build the "old-only" package.

1. merges the CSV tables (target schema, source rows)
2. drops item/block rows that only exist in the target client, pruning references
3. overlays assets, but only for entries that exist in both versions

    python tools/build_old_only.py <base.pkg> <clean.apk> <out.pkg> [--no-drop]
"""
from __future__ import annotations

import importlib.util
import io
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_TYPES = "png,ogg,omod,ent,emo,oanim,obj,vox,otex"
# tables whose rows reference item ids in one of their columns
REF_TABLES = ("crafting.csv", "dropitems.csv", "chestdef.csv", "furnace.csv", "tooldef.csv")


def load(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, name + ".py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def parse(merge, text):
    lines = [ln for ln in text.splitlines() if ln.strip()]
    header = [c.strip() for c in lines[1].split(",")]
    rows = []
    for ln in lines[2:]:
        f = ln.split(",")
        if f and f[0].strip():
            rows.append(f)
    return lines[0], lines[1], header, rows


def ids_of(rows):
    return {r[0].strip() for r in rows}


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    base, apk_path, out = argv[0], argv[1], argv[2]
    drop = "--no-drop" not in argv
    items_only = "--items-only" in argv

    merge = load("table_merge")
    norm = load("png_normalize")

    with zipfile.ZipFile(base) as zin:
        pack_name = [n for n in zin.namelist() if n.endswith(".apk")][0]
        new_pack = zipfile.ZipFile(io.BytesIO(zin.read(pack_name)))
        order = [n for n in new_pack.namelist() if not n.endswith("/")]
        entries = {n: new_pack.read(n) for n in order}

    src = zipfile.ZipFile(apk_path)
    src_names = set(src.namelist())

    # ---- old ids (source of truth for "exists in source") ------------------
    old_ids = {}
    for csv_name in ("itemdef.csv", "blockdef.csv"):
        text = merge.decrypt_text(src.read("assets/csvdef/" + csv_name))
        _, _, _, rows = parse(merge, text)
        old_ids[csv_name] = ids_of(rows)
    print(f"source: {len(old_ids['itemdef.csv'])} items, {len(old_ids['blockdef.csv'])} blocks")

    dropped_total = 0
    merged = 0
    dropped_ids = set()

    for name in order:
        if not name.endswith(".csv"):
            continue
        rel = name[len("assets/"):]
        if name not in src_names:
            continue
        target_text = merge.decrypt_text(entries[name])
        source_text = merge.decrypt_text(src.read(name))
        merged_text, _a, _b = merge.merge_table(target_text, source_text)
        h0, h1, header, rows = parse(merge, merged_text)

        csv_file = rel.split("/")[-1]
        if drop and csv_file in old_ids and not (items_only and csv_file == "blockdef.csv"):
            keep_ids = old_ids[csv_file]
            before = len(rows)
            rows = [r for r in rows if r[0].strip() in keep_ids]
            gone = before - len(rows)
            if gone:
                dropped_total += gone
                dropped_ids |= {r[0].strip() for r in parse(merge, merged_text)[3]} - keep_ids
                print(f"  {csv_file}: dropped {gone} new-only rows")

        if drop and csv_file in REF_TABLES and dropped_ids:
            before = len(rows)
            rows = [r for r in rows if not any(v.strip() in dropped_ids for v in r[1:])]
            gone = before - len(rows)
            if gone:
                print(f"  {csv_file}: pruned {gone} rows referencing dropped ids")

        body = [h0, h1] + [",".join(r) for r in rows]
        entries[name] = merge.encrypt_text("\n".join(body) + "\n")
        merged += 1

    # ---- assets: only entries that exist in the source APK ----------------
    # textures used by rows we kept stay eligible; everything is still matched by path
    replaced = 0
    skipped = 0
    types = tuple("." + t for t in DEFAULT_TYPES.split(","))
    for name in order:
        if not name.lower().endswith(types):
            continue
        if name not in src_names:
            continue
        raw = src.read(name)
        if name.lower().endswith(".png"):
            old = merge.unwrap_asset(raw)
            if old[:8] != b"\x89PNG\r\n\x1a\n":
                continue
            ref = merge.unwrap_asset(entries[name])
            if merge.png_size(old) != merge.png_size(ref):
                skipped += 1
                continue
            raw = norm.normalise(old, ref)
        entries[name] = merge.apply_envelope(entries[name], raw)
        replaced += 1

    print(f"csv rebuilt: {merged}, assets replaced: {replaced}, png size-mismatch: {skipped}")
    print(f"rows dropped: {dropped_total}")

    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as zout:
        for name in order:
            zout.writestr(name, entries[name])
    pack = buf.getvalue()

    with zipfile.ZipFile(base) as zin, zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED,
                                                       compresslevel=6) as zout:
        for item in zin.infolist():
            if item.filename == pack_name:
                zout.writestr(item, pack)
            elif item.filename.endswith("/"):
                zout.writestr(item, b"")
            else:
                zout.writestr(item, zin.read(item.filename))
    print("wrote", out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
