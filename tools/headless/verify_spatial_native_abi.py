#!/usr/bin/env python3
"""Read-only checks of the GTA 2.10 ARM64 model bounds used by spatial queries.

No game binary is copied or bundled. These checks do not replace device tests.
"""
import pathlib
import struct
import sys

root = pathlib.Path(__file__).resolve().parents[2]
path = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else root / "app/src/main/jniLibs/arm64-v8a/libGTASA.so"
data = path.read_bytes()
h = struct.unpack_from("<16sHHIQQQIHHHHHH", data)
assert h[0][:6] == b"\x7fELF\x02\x01" and h[2] == 183
sections = [struct.unpack_from("<IIQQQQIIQQ", data, h[6] + i*h[11]) for i in range(h[12])]
segments = [struct.unpack_from("<IIQQQQQQ", data, h[5] + i*h[9]) for i in range(h[10])]
symbols = {}
for s in sections:
    if s[1] != 11:
        continue
    strings = sections[s[6]]
    for offset in range(s[4], s[4]+s[5], s[9]):
        name, _, _, index, address, _ = struct.unpack_from("<IBBHQQ", data, offset)
        if index:
            start = strings[4]+name
            symbols[data[start:data.index(b"\0", start)].decode()] = address

def word(symbol, offset):
    address = symbols[symbol]+offset
    for s in segments:
        if s[0] == 1 and s[3] <= address < s[3]+s[5]-3:
            return struct.unpack_from("<I", data, s[2]+address-s[3])[0]
    raise AssertionError(f"unmapped: {symbol}+{offset:#x}")

guards = {
    "_ZN15CClumpModelInfo14GetBoundingBoxEv": {0:0xf9401800,4:0xd65f03c0},
    "_ZNK7CEntity14GetBoundCentreER7CVector": {0x18:0x79806509,0x2c:0xf9401929,0x30:0x91006122},
    "_ZN10CColSphere3SetEfRK7CVectorhhh": {0:0xbd000c00},
    "_ZN9CColModelaSERKS_": {0x0c:0x3cc18020,0x18:0x3c818260},
}
checks = 0
for symbol, instructions in guards.items():
    for offset, expected in instructions.items():
        assert word(symbol, offset) == expected, f"layout mismatch: {symbol}+{offset:#x}"
        checks += 1
assert "_ZN10CModelInfo16ms_modelInfoPtrsE" in symbols
print(f"Spatial model bounds ABI: {checks + 1} checks passed (instructions/exports only)")
