"""build_package — one command that produces a rebuilt package.

Pipeline (the reason this repo exists):

    base   = base.pkg            (the newer package)
    data   = old/assets          (source tables / models / textures / worlds)
    steps  = 1. merge source rows into the target schema
             2. (optional) overlay source art, re-wrapping .omod envelopes
             3. rebuild the data pack + repack the package
             4. verify: only the pack changed, binary untouched, entry counts stable
    output = output.pkg           (unsigned; sideload to re-sign)

    python tools/build_package.py --base base.pkg --assets old/assets --out output.pkg
    python tools/build_package.py ... --art          # also take source models/textures/sounds
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import io
import os
import sys
import zipfile

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


HERE = os.path.dirname(os.path.abspath(__file__))
PACK_ENTRY = None
BINARY_ENTRY = None


def load_merge_module():
    path = os.path.join(HERE, "table_merge.py")
    spec = importlib.util.spec_from_file_location("table_merge", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def verify(base_path: str, out_path: str) -> bool:
    """Only the data pack may differ; the executable and entry set must be identical."""
    with zipfile.ZipFile(base_path) as za, zipfile.ZipFile(out_path) as zb:
        names_a = {i.filename for i in za.infolist() if not i.filename.endswith("/")}
        names_b = {i.filename for i in zb.infolist() if not i.filename.endswith("/")}
        ok = True
        if names_a != names_b:
            print(f"  [FAIL] entry set changed: +{sorted(names_b - names_a)[:4]} "
                  f"-{sorted(names_a - names_b)[:4]}")
            ok = False
        else:
            print(f"  [ok] entry set unchanged ({len(names_a)} files)")

        pack_a = _pack_entry(za)
        binary_a = _app_bundle(za) + _app_bundle(za).rstrip("/").rsplit("/", 1)[-1][:-4]
        if digest(za.read(binary_a)) != digest(zb.read(binary_a)):
            print("  [FAIL] Mach-O executable changed")
            ok = False
        else:
            print("  [ok] executable untouched")

        changed = [n for n in sorted(names_a) if digest(za.read(n)) != digest(zb.read(n))]
        if changed == [pack_a]:
            print("  [ok] only the data pack differs")
        else:
            print(f"  [warn] unexpected changes: {changed[:6]}")
            ok = False

        pack_b_name = _pack_entry(zb)
        pack_a_zip = zipfile.ZipFile(io.BytesIO(za.read(pack_a)))
        pack_b = zipfile.ZipFile(io.BytesIO(zb.read(pack_b_name)))
        pack_a = pack_a_zip
        ea = {n for n in pack_a.namelist() if not n.endswith("/")}
        eb = {n for n in pack_b.namelist() if not n.endswith("/")}
        if ea != eb:
            print(f"  [warn] pack entries changed: +{len(eb - ea)} -{len(ea - eb)}")
        inner = [n for n in sorted(ea & eb)
                 if digest(pack_a.read(n)) != digest(pack_b.read(n))]
        kinds = sorted({n.rsplit(".", 1)[-1] for n in inner})
        print(f"  [ok] pack: {len(eb)} entries, {len(inner)} replaced ({', '.join(kinds)})")
    return ok


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="rebuild a package from an older asset tree")
    ap.add_argument("--base", required=True, help="base package")
    ap.add_argument("--assets", required=True, help="old/assets")
    ap.add_argument("--out", required=True, help="output package")
    ap.add_argument("--art", action="store_true", help="also overlay source art assets")
    ap.add_argument("--no-verify", action="store_true")
    args = ap.parse_args(argv)

    if not os.path.isfile(args.base):
        print(f"missing input package: {args.base}", file=sys.stderr)
        return 2
    if not os.path.isdir(args.assets):
        print(f"missing assets dir: {args.assets}", file=sys.stderr)
        return 2

    merge = load_merge_module()
    merge_args = [args.base, args.assets, args.out]
    if args.art:
        merge_args.append("--overlay-art")

    print("== 1/2 merge + repack ==")
    rc = merge.main(merge_args)
    if rc != 0:
        return rc

    if not args.no_verify:
        print("\n== 2/2 verify ==")
        if not verify(args.base, args.out):
            print("\nverification failed — do not ship this package", file=sys.stderr)
            return 1

    size = os.path.getsize(args.out) / 1048576
    print(f"\nready: {args.out}  ({size:.1f} MB, unsigned)")
    print("next : sideload with Sideloadly / AltStore (arm64 devices only)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
