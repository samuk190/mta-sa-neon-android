#!/usr/bin/env python3
"""Repair the known truncated mobilesc0 RLE stream, preserving existing pixels."""
import argparse
import hashlib
import struct
from pathlib import Path

DAT_SHA = "99a042f54465c00c780c8f5a7dafdadea6b4c773762c99d551a46b96dfa90af9"
TOC_SHA = "57df8ad68dabf02c3b17056e9d025454a832d8c9dcece2662d2254e6f917bc08"
FIXED_DAT_SHA = "709da114be8fd322ef538939ef9f1576d3164b630bb71bbc2fa7f9b33a96b6f9"
FIXED_TOC_SHA = "51b43d040b136c794d00ee01b1c115e26f03eda8fbf1d25072c8074173f9c8f5"
TEXTURE_OFFSET = 0x559D62
EXPECTED_BYTES = 1280 * 960 * 2


def digest(data):
    return hashlib.sha256(data).hexdigest()


def decode_rle(source, marker, limit):
    """Bounded equivalent of the engine's four-byte RLE unit decoder."""
    output = bytearray()
    position = 0
    while position < len(source):
        if source[position] == marker:
            if position + 6 > len(source):
                raise ValueError("Truncated RLE repeat")
            count = source[position + 1]
            block = source[position + 2:position + 6]
            position += 6
        else:
            if position + 4 > len(source):
                raise ValueError("Truncated RLE literal")
            count = 1
            block = source[position:position + 4]
            position += 4
        if len(output) + count * 4 > limit:
            raise ValueError("RLE output exceeds the texture dimensions")
        output.extend(block * count)
    return output


def repair(source_dir, output_dir):
    data = (source_dir / "menu.pvr.dat").read_bytes()
    toc = (source_dir / "menu.pvr.toc").read_bytes()
    if digest(data) != DAT_SHA or digest(toc) != TOC_SHA:
        raise ValueError("Input is not the known truncated texture database")
    _, pixel_format, width, height, size, marker = struct.unpack_from("<HHHHII", data, TEXTURE_OFFSET)
    if (pixel_format, width, height, marker) != (0x8363, 1280, 960, 0x1A):
        raise ValueError("Unexpected texture header")
    stream = data[TEXTURE_OFFSET + 16:]
    if len(stream) != size - 4 or struct.unpack_from("<I", toc)[0] != len(data):
        raise ValueError("Unexpected texture/table lengths")
    original_pixels = decode_rle(stream, marker, EXPECTED_BYTES)
    if len(original_pixels) != EXPECTED_BYTES - 12 or original_pixels[-2:] != bytes.fromhex("6762"):
        raise ValueError("Unexpected truncated image")
    # The original six final RGB565 pixels are unavailable. Extend the last
    # known pixel to those positions, leaving every existing decoded byte intact.
    extension = original_pixels[-2:] * 6
    fixed_pixels = decode_rle(stream + extension, marker, EXPECTED_BYTES)
    if len(fixed_pixels) != EXPECTED_BYTES or fixed_pixels[:-12] != original_pixels:
        raise ValueError("Texture repair failed its bounds/prefix check")
    repaired_data = bytearray(data + extension)
    repaired_toc = bytearray(toc)
    struct.pack_into("<I", repaired_data, TEXTURE_OFFSET + 8, size + len(extension))
    struct.pack_into("<I", repaired_toc, 0, len(repaired_data))
    if digest(repaired_data) != FIXED_DAT_SHA or digest(repaired_toc) != FIXED_TOC_SHA:
        raise ValueError("Unexpected repaired database checksum")
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "menu.pvr.dat").write_bytes(repaired_data)
    (output_dir / "menu.pvr.toc").write_bytes(repaired_toc)
    print(f"Repaired mobilesc0: {len(fixed_pixels)} decoded bytes, six final pixels extended")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_dir", type=Path)
    parser.add_argument("output_dir", type=Path)
    args = parser.parse_args()
    repair(args.source_dir, args.output_dir)
