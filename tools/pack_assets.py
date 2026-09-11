#!/usr/bin/env python3
"""Pack extracted assets/ into ASSETS.DAT (8.3 filename, internal POSIX paths)."""

from __future__ import annotations

import hashlib
import json
import os
import struct
import sys
from pathlib import Path

MAGIC = b"SMBPAK1\0"
NAME_LEN = 96
ENTRY_FMT = f"<{NAME_LEN}sII"
ENTRY_SIZE = struct.calcsize(ENTRY_FMT)


def collect(root: Path) -> list[tuple[str, bytes]]:
    manifest_path = root / "manifest.json"
    if manifest_path.is_file():
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        if manifest.get("schema") != "smb2-assets-v1":
            raise SystemExit("unsupported asset manifest schema")
        files = []
        seen = set()
        for entry in manifest.get("assets", []):
            rel = entry["path"]
            if rel in seen or rel.startswith("/") or ".." in Path(rel).parts:
                raise SystemExit(f"invalid or duplicate manifest path: {rel}")
            seen.add(rel)
            full = root / rel
            if not full.is_file():
                raise SystemExit(f"manifest asset missing: {rel}")
            data = full.read_bytes()
            if len(data) != entry["size"]:
                raise SystemExit(f"manifest size mismatch: {rel}")
            if hashlib.sha256(data).hexdigest() != entry["sha256"]:
                raise SystemExit(f"manifest hash mismatch: {rel}")
            files.append((rel, data))
        if not files:
            raise SystemExit("asset manifest is empty")
        return files

    files: list[tuple[str, bytes]] = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames.sort()
        for name in sorted(filenames):
            if name.startswith("."):
                continue
            full = Path(dirpath) / name
            rel = full.relative_to(root).as_posix()
            if len(rel.encode("ascii", "replace")) >= NAME_LEN:
                raise SystemExit(f"path too long for pack: {rel}")
            files.append((rel, full.read_bytes()))
    if not files:
        raise SystemExit(f"no files under {root}")
    return files


def pack(root: Path, dest: Path) -> None:
    files = collect(root)
    header = 8 + 4 + ENTRY_SIZE * len(files)
    offset = header
    entries = []
    payload = bytearray()
    for rel, data in files:
        name = rel.encode("ascii")
        entries.append(struct.pack(ENTRY_FMT, name, offset, len(data)))
        payload.extend(data)
        offset += len(data)
    blob = bytearray()
    blob.extend(MAGIC)
    blob.extend(struct.pack("<I", len(files)))
    blob.extend(b"".join(entries))
    blob.extend(payload)
    dest.write_bytes(blob)
    print(f"Packed {len(files)} files ({len(blob)} bytes) -> {dest}")


def main() -> None:
    if len(sys.argv) != 3:
        print("usage: pack_assets.py <assets_dir> <ASSETS.DAT>", file=sys.stderr)
        sys.exit(2)
    pack(Path(sys.argv[1]), Path(sys.argv[2]))


if __name__ == "__main__":
    main()
