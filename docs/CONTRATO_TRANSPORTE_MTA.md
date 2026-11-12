# MTA Recovered Network Transport Contract

Static investigation audit as of 2026-11-11. The primary implementable unit is the opening datagram and response parser codec for the native MTA transport. The remainder of the session relies on verified initialization constants and interoperability vectors.

---

## 1. Source Artifacts & Traceability

| Local Artifact | Identification |
|---|---|
| `mta_decompile_android_progress/libWorldRPG/source/libWorldRPG_decompiled.c` | Ghidra pseudo-C export, 55,815,326 bytes, SHA-256 `4704e5a9c6db286180fce99347a00023a9974c623092999f6e027f140ec57e96` |
| `mta_decompile_android_progress/libWorldRPG/libs/libWorldRPG.so` | Original MobileSA library, ELF64 AArch64 Android, 19,291,424 bytes, SHA-256 `6843ffb45dfee25be9b93d542499b66cfe1822c748a080e64a3ffdfc40dd9590` |
| `MobileSA-release-216c1e7f.apk` | Reference library with SHA-256 `6c554c287586d0e19a438e0a727243394d51cb7fbd2208833b61b7407e1de7c1`. Inception opening structure verified identical |
| `net.so` (Server build) | ELF64 AArch64 Linux, SHA-256 `e6d7ea540ff223927d2ab39474050b6747eb2f64d79422bd11eb683b3d4a7338`, build-id `9eaac55d5e599d7b622c7e35ba43fddb889c6906` |
| `mta_mobile_neon/libs/netc_pc.dll` | Windows x86 desktop client binary used for static magic verification |

---

## 2. Ghidra Address Resolution

The Ghidra program base is `0x100000`. Addresses in pseudo-C include this offset. In the examined ELF, the true RVA is the Ghidra address minus `0x100000`.

- `JNI_OnLoad` appears at `0x00697780` in pseudo-C and at `0x00597780` in the ELF symbol table.
- The opening constructor appears as `FUN_00e0f4c0` in pseudo-C. At true RVA `0x00d0f4c0`, the assembly writes `0x0409` at the beginning of the buffer and `0xDE` at offset `0x32`.

---

## 3. Discovered Network Routines

All line references correspond to `libWorldRPG_decompiled.c`:

| Ghidra Function | True RVA | Start Line | Recovered Behavior |
|---|---|---|---|
| `FUN_00e0da40` | `0x00d0da40` | 1027389 | Connection preparation. Calls constructor, buffers opening packet, and sends with length `0x33` (51 bytes) |
| `FUN_00e0f4c0` | `0x00d0f4c0` | 1028509 | Constructs 51-byte opening packet and records 16-byte nonce |
| `FUN_00e0f630` | `0x00d0f630` | 1028580 | Sockets `sendto`, transmission counters, and telemetry |
| `FUN_00e0fb00` | `0x00d0fb00` | 1028852 | `recvfrom`, verifies source IP/port and dispatches to parser |
| `FUN_00e104e0` | `0x00d104e0` | 1029706 | Opening response parser and incoming encrypted datagram receiver |
| `FUN_00e1a860` | `0x00d1a860` | 1036436 | Encapsulates encrypted output with 16-bit checksum and stream selector byte |
| `FUN_00e23b00` | `0x00d23b00` | 1043390 | Reliable queue dispatch (reliability layer, channels, timestamps) |
| `FUN_010b11fc` | `0x00fb11fc` | 1508435 | 16-bit CRC checksum using polynomial `0x1021` |
| `FUN_010b12f0` | `0x00fb12f0` | 1509064 | Cipher state initialization and 256-byte S-box permutation |
| `FUN_010b1494` | `0x00fb1494` | 1509227 | Outgoing stream byte transform |
| `FUN_010b14dc` | `0x00fb14dc` | 1509252 | Incoming stream byte transform |
| `FUN_010b2598` | `0x00fb2598` | 1510212 | Bitstream reliability frame parser |

---

## 4. 51-Byte Opening Structure (OPEN51)

| Offset | Length | Content | Notes |
|---|---|---|---|
| 0 | 1 | `0x09` (Opening ID) | Confirmed immediate assembly byte |
| 1 | 1 | `0x04` (Protocol Mode) | Fixed field |
| 2 | 16 | Client Connection Nonce | Generated randomly or via PRNG; copied to connection state |
| 18 | 16 | MTA Protocol Magic | Verified across client and server binaries (see below) |
| 34 | 16 | Fixed Parameter Block | Version and capability negotiation parameters |
| 50 | 1 | `0xDE` | Terminator byte |

### Verified MTA Magic Sequence
```text
00 ff 00 ff fe fe fe fe fd fd fd fd 78 56 34 17
```

Confirmed independently in:
1. Android native binary parser comparing two 64-bit immediates (`00ff00fffefefefe` and `fdfdfdfd78563417`).
2. Server `net.so` at offset `0x434c30`.
3. Desktop `netc_pc.dll` at file offset `0x536da0`.

---

## 5. Opening Response & Extension Fields

In the opening state, `FUN_00e104e0` parses the response format before cipher initialization:

| Offset | Content |
|---|---|
| 0 | Identifier `0x0A` |
| 1–16 | MTA Protocol Magic |
| 17–32 | Server Nonce / Session Token |
| 33 | Extension marker `0xED` |
| 34+ | TLV attributes (1-byte tag, 1-byte length, payload) |
| End | Section terminator `0xDE` |

### Recognized TLV Tags:
- `0x6E`: Up to 64 bytes of key derivation material.
- `0x1A`: Up to 16 bytes prefix / nonce tag.
- `0xA8`: 32-bit big-endian integer (connection parameters).
- `0xC8`: 32-bit big-endian integer (buffer / MTU limits).

---

## 6. Native Offline Codec (`MtaOfflineCodec`)

The production implementation in `app/src/main/cpp/net/mta/MtaOfflineCodec.{h,cpp}` encapsulates:
1. `MtaOfflineCodec::BuildOpen51`: Constructs deterministic or randomized 51-byte opening datagrams.
2. `MtaOfflineCodec::ParseOpenResponse`: Validates magic, extracts server tokens, and decodes TLV extension fields.
3. Unit tests in `tests/test_mta_offline.cpp` validate strict boundaries, rejecting malformed tags, truncated buffers, and invalid trailing bytes.
