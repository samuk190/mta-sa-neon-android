#!/usr/bin/env python3
"""Read-only checks of the GTA 2.10 ARM64 layout used by ped queries.

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
    "_ZN4CPed16SetCurrentWeaponEi": {0x1c:0x39a37268, 0x24:0xb9473120, 0x6c:0x39237274},
    "_ZN7CWeapon6ReloadEP4CPed": {0x10:0xb9400e68, 0x50:0xb9000a68},
    "_ZN7CWeapon10InitialiseE11eWeaponTypeiP4CPed": {0x28:0xb9000274,0x30:0xb9000e68},
    "_ZN16CPedIntelligenceC2EP4CPed": {0x18:0x91002260},
    "_ZN12CTaskManager7SetTaskEP5CTaskib": {0x14:0xf862da60,0x3c:0xf8367a74},
}
checks = 0
for symbol, instructions in guards.items():
    for offset, expected in instructions.items():
        assert word(symbol, offset) == expected, f"layout mismatch: {symbol}+{offset:#x}"
        checks += 1
for name, task in [("14CTaskSimpleDie",212),("16CTaskSimpleDrown",213),
                   ("19CTaskSimpleDieInCar",214),("20CTaskComplexDieInCar",215),
                   ("21CTaskSimpleDrownInCar",216),("15CTaskComplexDie",217),("15CTaskSimpleDead",218)]:
    symbol = f"_ZNK{name}11GetTaskTypeEv"
    assert word(symbol,0) == 0x52800000 | (task << 5), f"task enum mismatch: {symbol}"
    assert word(symbol,4) == 0xd65f03c0, f"task accessor mismatch: {symbol}"
    checks += 2
print(f"Ped query native ABI: {checks} checks passed (instructions only)")
