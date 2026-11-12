# Stage S1 - ENTITY_ADD core and dispatch

Prerequisite for S2 and S3. Everything else waits for this stage's acceptance.

## Goal

Make `OnEntityAdd` (game packet 50, wire 149) parse every element in the packet, for every type the server sends, so the next element header is always at the right bit.

## Files you own

- `app/src/main/cpp/shared/ServerWorld.cpp` (the `OnEntityAdd`, `ReadCustomBlock`, `SkipName`, `ReadLuaValue` functions and their `DIAG` helpers)
- NEW `app/src/main/cpp/shared/entity/EntityAddTypes.h`
- NEW `app/src/main/cpp/shared/entity/EntityAddCore.cpp` (common header, dispatch loop)
- NEW `app/src/main/cpp/shared/entity/EntityAddDummy.cpp`, `EntityAddMarker.cpp` (and BLIP/PICKUP/other non-ped non-vehicle types in `EntityAddOther.cpp`)
- NEW stubs (you create them, S2/S3 fill them): `app/src/main/cpp/shared/entity/EntityAddPed.cpp`, `EntityAddVehicle.cpp`, `EntityAddObject.cpp`

Do not edit `PacketHandler.cpp`, `GameEngine.cpp` or `NativeTaskSync.h`.

## Interface to create (`EntityAddTypes.h`)

```cpp
namespace WorldRPG::Entity {
struct EntityCommon {
    uint32_t id = 0;
    uint8_t  type = 0;
    uint32_t parent = 0;
    uint8_t  interior = 0;
    uint32_t dimension = 0;
    bool     attached = false;
    uint32_t attachedTo = 0;
    float    attachedX = 0, attachedY = 0, attachedZ = 0;
    bool     collisions = false;
    bool     callProp = true;     // default true, as in the reference
};
// Returns false on a parse failure. The caller logs a DIAG line and stops the packet.
bool ReadEntityBodyPed(Net::BitStreamReader& reader, const EntityCommon& common, Visual& visual);
bool ReadEntityBodyVehicle(Net::BitStreamReader& reader, const EntityCommon& common, Visual& visual);
bool ReadEntityBodyObject(Net::BitStreamReader& reader, const EntityCommon& common, Visual& visual);
bool ReadEntityBodyDummy(Net::BitStreamReader& reader, const EntityCommon& common, Visual& visual);
bool ReadEntityBodyMarker(Net::BitStreamReader& reader, const EntityCommon& common, Visual& visual);
bool ReadEntityBodyOther(Net::BitStreamReader& reader, const EntityCommon& common, Visual& visual);  // BLIP, PICKUP, RADAR_AREA, COLSHAPE, etc.
}
```

`Visual` stays where it is today (move it to `EntityAddTypes.h` only if it is needed in the new files, and say so in your report).

## Work items

1. **Common header** (`EntityAddCore.cpp`): read in the reference order, `Packet_EntityAdd` lines ~2870-2990:
   - ElementID (17 bits), type (8 bits), parent ElementID, interior (8 bits), dimension (`ReadCompressed` 2 bytes), attached flag (1 bit).
   - If attached: attached-to ElementID, attached position, attached rotation (widths from the reference structs).
   - collisions (1 bit). callProp (1 bit) only if the effective peer bitstream is `>= 0x0031` (`kServerEntityBitStream`, currently `0x0030`, so not read).
   - Custom data: `ReadCompressed(usNumData)` (ushort), then per item: name length (uint8), name chars, `CLuaArgument::ReadFromBitStream`.
   - Name: `ReadCompressed(usNameLength)` (ushort), name chars.
   - Sync time context: one byte only if `Can(ExtendedWorldPositions)`. Not read at `0x0030`.
2. **Dispatch**: switch on type as in the reference (lines ~3132-3800). Call the body function from the table. A type the reference does not handle reads nothing and returns `true`.
3. **DUMMY** (0): no body. Reference has no DUMMY case.
4. **MARKER** (4): reference lines ~3775. Position, `SMarkerTypeSync`, float size, `SColorSync`, then checkpoint/ring target fields. Check every width against `SyncStructures.h`.
5. **BLIP, PICKUP, RADAR_AREA, COLSHAPE, SPAWNPOINT, WORLD_MESH, PATH_NODE, TEAM, CONSOLE, REMOTECLIENT**: port each case from the reference into `EntityAddOther.cpp`. Where the reference reads something version-gated, gate it the same way.
6. **Stubs** for PED, VEHICLE, OBJECT: create the three files with functions that log `DIAG ENTITY_ADD body not implemented type=N` and return `false`. This stops the packet at a known point. S2 and S3 replace these bodies.

Note on `false` from stubs: until S2/S3 land, a ped or vehicle element will stop the packet. That is acceptable for S1 but means the S1 acceptance check below can only be run on a server packet that has no ped or vehicle elements. Use the `DIAG` output to confirm which types appear. If the only element types present are the ones S1 handles, the check passes.

## Acceptance (device)

- No `DIAG ENTITY_ADD fail` lines for types S1 owns.
- `ENTITY_ADD stored N` (or equivalent DIAG line) matches the element count, for any packet that contains only S1 types.
- `DIAG ENTITY_ADD idx=...` headers show plausible ids (small numbers, parent ids that exist), with no type values outside 0-15.
- Logs keep the bit offset advancing by a plausible amount per element.

## Host check

```
g++ -std=c++20 -fsyntax-only -Wall -I app/src/main/cpp app/src/main/cpp/shared/ServerWorld.cpp
g++ -std=c++20 -fsyntax-only -Wall -I app/src/main/cpp app/src/main/cpp/shared/entity/EntityAddCore.cpp
```

Report each file you touched, the reference line for each case, and the host result.
