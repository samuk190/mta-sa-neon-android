#!/usr/bin/env python3
"""Check the native ABI, ELF LOADs and ZIP packing of the Neon Android APK."""
import argparse
import hashlib
import json
import struct
import zipfile
from pathlib import Path

PAGE_SIZE = 16384
ARM64 = 183
REQUIRED = {"libWorldRPGNeon.so", "libGTASA.so", "libOpenAL64.so", "libSCAnd.so"}


def audit_elf(data, name):
    if data[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<H", data, 18)[0] != ARM64:
        raise ValueError(f"{name}: expected a native Android ARM64 ELF")
    table = struct.unpack_from("<Q", data, 32)[0]
    stride, count = struct.unpack_from("<HH", data, 54)
    if stride < 56 or table + stride * count > len(data):
        raise ValueError(f"{name}: invalid ELF program headers")
    alignments = []
    for index in range(count):
        kind, _, offset, address, _, file_size, _, alignment = struct.unpack_from(
            "<IIQQQQQQ", data, table + index * stride
        )
        if kind != 1:
            continue
        if alignment < PAGE_SIZE or (address - offset) % PAGE_SIZE or offset + file_size > len(data):
            raise ValueError(f"{name}: invalid or non-16 KB-aligned LOAD")
        alignments.append(alignment)
    if not alignments:
        raise ValueError(f"{name}: no LOAD segments")
    return min(alignments)


def audit(path):
    libraries = {}
    with zipfile.ZipFile(path) as apk, path.open("rb") as archive:
        for entry in apk.infolist():
            if not entry.filename.startswith("lib/") or not entry.filename.endswith(".so"):
                continue
            parts = entry.filename.split("/")
            if len(parts) != 3 or parts[1] != "arm64-v8a":
                raise ValueError(f"Unexpected library ABI: {entry.filename}")
            name = parts[-1]
            if name in libraries or name == "net.so" or name.lower().startswith(("libmobilesa", "libnetc")):
                raise ValueError(f"Unexpected or duplicate native library: {entry.filename}")
            native = apk.read(entry)
            if entry.compress_type == zipfile.ZIP_STORED:
                archive.seek(entry.header_offset + 26)
                name_length, extra_length = struct.unpack("<HH", archive.read(4))
                data_offset = entry.header_offset + 30 + name_length + extra_length
                if data_offset % PAGE_SIZE:
                    raise ValueError(f"{name}: stored ZIP data is not 16 KB aligned")
                packaging = "stored, aligned"
            else:
                packaging = "compressed, extracted at install"
            libraries[name] = {
                "minimum_load_alignment": audit_elf(native, name),
                "packaging": packaging,
                "sha256": hashlib.sha256(native).hexdigest(),
            }
        if missing := REQUIRED - libraries.keys():
            raise ValueError(f"Missing required libraries: {', '.join(sorted(missing))}")
        if any(name.lower().endswith(".dll") for name in apk.namelist()):
            raise ValueError("Windows DLL found in the native Android APK")
    return {
        "apk": str(path),
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "libraries": libraries,
        "scope": "APK packaging only; execution requires device validation",
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("apk", type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(audit(args.apk), indent=2))
    except (OSError, ValueError, struct.error, zipfile.BadZipFile) as error:
        parser.exit(1, str(error) + "\n")
