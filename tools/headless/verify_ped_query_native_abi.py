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
    "_ZN10CPlaceable15InitMatrixArrayEv": {0x40:0x90001b08, 0x44:0xf946cd08},
    "_ZN17CVehicleModelInfo15SetupCommonDataEv": {
        0xd0:0x52951800, 0xdc:0x52800640, 0xe8:0x929ff9a8, 0x144:0xf100c91f,
    },
    "_ZN9CColPointaSERKS_": {4:0xb9000808, 12:0xf9000008, 0x44:0xb9002808},
    "_ZN7CCamera18GetGameCamPositionEv": {0:0x91239000, 4:0xd65f03c0},
    "_ZN7CWeapon10FireSniperEP4CPedP7CEntityP7CVector": {
        0x8c:0x52804509, 0x98:0x39417d0a, 0xcc:0x910bb10a, 0xdc:0xf9417108,
    },
    "_ZN11CWeaponInfo14LoadWeaponDataEv": {0x530:0xb9001b69},
    "_ZN22CTaskSimpleGangDriveBy10ProcessPedEP4CPed": {0x578:0x97f0fd56},
    "_ZN7CCamera17IsTargetingActiveEv": {0:0xf81e0ff3, 4:0xa9017bfd, 8:0x910043fd, 12:0xaa0003f3, 0x48:0x79501a68},
    "_ZN7CCamera22SetNewPlayerWeaponModeEsss": {0:0x79101801, 4:0x79102c03, 8:0x79102802, 12:0xb908101f},
    "_ZNK8CVehicle11IsPassengerEPK4CPed": {4:0xf942d408, 0x58:0xf942f008},
    "_ZNK22CTaskSimpleGangDriveBy5CloneEv": {0x1c:0x321b07e0},
    "_ZN22CTaskSimpleGangDriveBy12PlayerTargetEP4CPed": {
        0:0xd10103ff, 4:0xfd000be8, 8:0xa9024ff4, 12:0xa9037bfd,
        0x4c:0x78013289, 0x15c:0x39404a88, 0x1ac:0xb9002e88,
    },
    "_ZN6CPedIK19PointGunInDirectionEffbf": {0:0xd10183ff, 4:0xfd000bea, 8:0x6d01a3e9, 12:0xf90017f7},
    "_ZN17CTaskSimpleUseGun10ProcessPedEP4CPed": {
        0:0xd102c3ff, 4:0xfd002bea, 8:0x6d0623e9, 12:0xa9075ff8, 0xc74:0x39c06268, 0xc84:0x39405a68,
    },
    "_ZN16IKChainManager_c8PointArmEPciP4CPedP7CEntityiP5RwV3dfif": {0:0x6db82beb, 4:0x6d0123e9, 8:0xf90013fb, 12:0xa90367fa},
    "_ZN6CPedIK17RotateTorsoForArmERK7CVector": {0:0xfc1d0fe8, 4:0xa9014ff4, 8:0xa9027bfd, 12:0x910083fd},
    "_ZNK12CEventDamage10AffectsPedEP4CPed": {0:0xd10103ff, 4:0xf9000bf5, 8:0xa9024ff4, 12:0xa9037bfd},
    "_ZN7CWeapon4FireEP7CEntityP7CVectorS3_S1_S3_S3_": {0:0xd10243ff, 4:0x6d0223e9, 8:0xa9036ffc, 12:0xa90467fa},
    "_ZN7CWeapon14DoBulletImpactEP7CEntityS1_P7CVectorS3_P9CColPointi": {0:0xd10343ff, 4:0x6d0623e9, 8:0xa9076ffc, 12:0xa90867fa},
    "_ZN6CWorld18ProcessLineOfSightERK7CVectorS2_R9CColPointRP7CEntitybbbbbbbb": {
        0:0xd104c3ff, 4:0xfd005bea, 8:0x6d0c23e9, 12:0xa90d6ffc,
    },
    "_ZN7CWeapon31CheckForShootingVehicleOccupantEPP7CEntityP9CColPoint11eWeaponTypeRK7CVectorS8_": {
        0x38:0x39416aa8, 0x40:0x7100091f,
    },
    "_ZN7CWeapon14FireInstantHitEP7CEntityP7CVectorS3_S1_S3_S3_bb": {
        0x540:0x97ec8c5b, 0x710:0x97ec8be7,
        0x3bc:0x97ec7290, 0x500:0x97ec723f, 0x6cc:0x97ec71cc, 0xcf4:0x97ec7042, 0x1064:0x97ec6f66
    },
    "_ZN4CPed16SetCurrentWeaponEi": {0x1c:0x39a37268, 0x24:0xb9473120, 0x6c:0x39237274},
    "_ZN7CWeapon6ReloadEP4CPed": {0x10:0xb9400e68, 0x50:0xb9000a68},
    "_ZN7CWeapon10InitialiseE11eWeaponTypeiP4CPed": {0x28:0xb9000274,0x30:0xb9000e68},
    "_ZN16CPedIntelligenceC2EP4CPed": {0x18:0x91002260},
    "_ZN12CTaskManager7SetTaskEP5CTaskib": {0x14:0xf862da60,0x3c:0xf8367a74},
}
# The mobile pool allocators inline capacity, allocation size and bitmap setup.
# Verify the contiguous layouts that the startup patches will accept.
checks = 0
for symbol, start, size, expected in [
    ("_ZN6CPools10InitialiseEv", 0, 0x9e0, 0x5cacac08),
    ("_ZN24CCustomCarEnvMapPipeline10CreatePipeEv", 0, 0x1ec, 0x17c41fa5),
    ("_ZN10CStreaming5Init2Ev", 0x450, 0xbc, 0xc109f0b4),
]:
    checksum = 2166136261
    for off in range(start, start + size, 4):
        for byte in word(symbol, off).to_bytes(4, "little"):
            checksum = ((checksum ^ byte) * 16777619) & 0xffffffff
    assert checksum == expected, (symbol, hex(checksum))
    checks += 1
assert symbols["_ZN10CPlaceable15InitMatrixArrayEv"] - symbols["_ZN15CMatrixLinkList4InitEi"] == 0xe34
checks += 1
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
