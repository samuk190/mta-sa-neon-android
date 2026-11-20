# Technical decisions

## 1. Publish the pose outside `g_mutex`

`PublishPose` copies the stored state under `g_mutex`, then calls `ServerWorld::ElementPose`,
`ServerWorld::ModelOf` and `Game::NoteRemoteVehicle` after releasing it. Those calls take
`ServerWorld::g_lock` and `GameEngine::g_remoteMutex`; keeping `g_mutex` held would create a new
lock order between the vehicle table and the game layer. The skill requires the network thread to
only queue under the game mutex, which this does.

Rejected: publishing directly from `ApplyUnoccupied` with the update fields. It would have to guess
whether a rotation was known, and it would break the one-setter-per-desktop-call structure that
`Packet_UnoccupiedVehicleSync` uses.

## 2. Rotation-only entries keep the previous position

`PublishPose` calls `ServerWorld::ElementPose` when no position has been stored yet, instead of
publishing (0,0,0). Desktop `SetTargetRotation` only changes the rotation of an element that already
has a position. A zero pose would drag the car to the origin and is worse than skipping the update.

## 3. Publish on both setters

`ApplyUnoccupied` calls `SetTargetPosition` then `SetTargetRotation` (desktop order). Both publish,
so the second call carries the final pose. `ApplyRemoteCars` runs later on the game thread and reads
the last stored record, so the intermediate publish is never rendered. This also covers packets that
carry only one of the two flags.

## 4. Vehicle motion stays log-only for now

`NoteRemoteVehicleMotion` exists and `ApplyCarMotion` writes `CPhysical +0x68/+0x74`, but it takes
move and turn speed together and clears one `motionPending` flag. Packet 56 sets the two fields
independently (`syncVelocity`, `syncTurnVelocity`). Wiring it as-is would write a stale value for the
flag that is absent. The faithful fix is two setters or a per-field pending record; deferred to a
separate contribution so this one stays small and testable.

## 5. Vendored Opus fallback in the host build

The host suite used `pkg_check_modules(... REQUIRED opus)` and failed to configure on a clean Linux
machine. The Android build already compiles the vendored Opus 1.4 tree, and it builds and links on
the host (`libopus.a`, verified). The host build now prefers the system package and falls back to the
vendored target. No production code changes.

Rejected: installing `libopus-dev` in this environment (no passwordless sudo) and vendoring a
prebuilt `.pc` file (not reproducible).

## 6. Test the packet through `PacketHandler`, not the private apply function

`HandleUnoccupiedVehicleSync` is in an anonymous namespace, so the test builds a real packet-56 body
with `WriteUnoccupiedEntry`, writes the game id `56`, and calls
`PacketHandler::handlePacket(99, ...)` exactly like the join path. It then reads the pose through a
test-only `Headless::CopySeenVehicle` accessor over the existing `NoteRemoteVehicle` record. This
covers the decoder, the loop and the apply in one check, without adding production API.
