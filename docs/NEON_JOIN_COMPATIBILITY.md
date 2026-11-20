# Standard/legacy joins and SA:O Identity

The connection dialog saves a connection mode per endpoint:

| Mode | Logical join ID | Selection |
| --- | --- | --- |
| Automatic | 4 | Uses the standard Neon handshake on every endpoint. |
| Standard | 4 | Native Neon network envelope followed by `PLAYER_JOINDATA`. |
| Legacy mobile | 19 | Captured 763-byte MobileSA envelope for servers explicitly supporting the adapter. |

There is one join format per connection. Timeout, authentication failure, and server refusal never silently change formats or downgrade authentication.

## Why the legacy packet existed

The public MTA enum reserves logical ID 19 (`PACKET_ID_MTA_RESERVED_13`). The game-layer join is ID 4. The World RPG mobile adapter translates the legacy MobileSA envelope. Legacy mode keeps the 667 padding bytes, the version marker at offset 676, and the game body at 668.

A direct game-layer packet 4 is insufficient: `net.so` consumes a network-level prefix before forwarding the body to `CGame`. `NetJoinEnvelope.h` supplies the prefix; it does not patch the server or mark a connection authenticated. The server remains responsible for accepting the peer and checking its ticket and capabilities.

The network prefix was checked against ARM64 `net.so` SHA-256 `e6d7ea540ff223927d2ab39474050b6747eb2f64d79422bd11eb683b3d4a7338`, reader `0x13d9c0..0x13e4c8`. It carries peer layout, encoded installation identity, version fields/checksum and an empty proprietary module-report vector, then byte-aligns the game body. Android does not fabricate a Windows anti-cheat report. Servers requiring additional client checks may refuse the client.

The Android serial is an app-scoped device identifier, independent of IP and the SA:O session. Headless probes use a random identifier per process. The game serializer follows desktop `CClientGame::StartGame` (`CClientGame.cpp:715-769`) and `CPlayerJoinDataPacket::Read`, including password MD5, the 32-byte legacy community field, and the trailing ticket.

Both profiles use the implemented `0x0030` peer layout. Legacy retains its captured `0x003D` announcement because the deployed adapter rewrites it to `0x0030`; standard advertises `0x0030` directly. A higher server version in `MOD_NAME` does not mean Android supports that layout. Servers requiring newer Neon capabilities can still reject this client.

In earlier tests the deployed World RPG server refused a standard join with AC #4. Players connecting to servers that still require the mobile adapter can select Legacy mobile in the connection dialog.

## Client platform

Both Android join profiles write the zero-padded `NEON:ANDROID` marker in the existing 32-byte community field. The matching desktop client writes `NEON:DESKTOP`. The Neon server exposes the declared value as the ninth argument of the server-side `onPlayerConnect` event and via `getPlayerPlatform(player)`. The values are `android`, `desktop` and `unknown` for clients without a recognized marker. The platform is self-reported connection metadata.

## SA:O account

Settings provides **Sign in with Discord** and **Sign out**. The browser flow follows desktop `CNeonIdentityManager`:

1. POST `/v1/auth/discord/start` to `https://identity.mta-neon.com`.
2. Validate the returned authorization address and open the system browser.
3. Poll `/v1/auth/discord/poll` for up to five minutes; allow cancellation.
4. Store the session encrypted with an Android Keystore AES-GCM key, outside backups. A file lock synchronizes launcher and `:engine` access.
5. For each connection, POST `/v1/tickets` with the server ID and the actual connected IPv4 endpoint obtained from the UDP socket. Send the resulting ticket once in the join body. Reconnection obtains another ticket.

Disabled authentication does not contact Identity. Optional authentication joins anonymously when signed out; a signed-in account obtains a ticket. Required authentication prompts sign-in. Browser login cancels the pending game handshake and starts a fresh connection afterward. Ticket requests have their own bounded waiting state. Request IDs discard stale responses after cancellation/reconnect.

The existing legacy adapter truncates its extracted body to 256 bytes. Until that adapter is upgraded, legacy mode explicitly refuses nonempty Identity tickets instead of truncating them or bypassing required sign-in. Standard mode supports tickets up to the server's 4096-byte limit.

Sessions, bearer tokens, poll secrets and tickets are not written to application logs. The game account's nickname/password login remains separate from SA:O sign-in.

## Verification

Run the native codec/state regression target with:

```sh
NEON_HOST_TEST_BUILD_DIR=/tmp/neon-headless-net bash tests/run_host_tests.sh
```

The tests cover profile selection, legacy offsets/size, independently decoded standard fields, MD5 reference bytes, a server-accepted network-header vector, long tickets, and cancellation/stale-request handling. The serialized final partial byte must be copied MSB-aligned: losing its last bit invalidates real Ed25519 JWT signatures while anonymous joins still work.

`tools/headless/run_identity_tests.py` runs the production Kotlin API against an in-memory service and covers start/poll/ticket contracts, endpoint binding, rejected authorization addresses, expired sessions and oversized tickets. It needs the existing Kotlin/Coroutine jars and a JVM JSON jar, as the browser test runner does.

Network validation uses loopback-only Oracle test containers with the production server image and no mobile adapter. A separate authentication test server trusts an ephemeral test-only Ed25519 key; its tickets never authenticate to production. The headless client's optional arguments after `[mode]` are `[auto|standard|legacy] [ticket-file]`; ticket contents are not command-line arguments or log output.

Live checks completed during implementation include full legacy World RPG entry (28 resource packets parsed without errors), a 900-second standard session without an adapter (final state complete, 8827 player-sync packets sent), and required Identity entry with a signed 1911-byte test ticket. The server rejected replayed tickets, invalid signatures, incorrect audiences and expired tickets. Optional mode accepted anonymous and authenticated joins and rejected an invalid supplied signature.

The debug APK for device testing bundles `libGTASA.so`, `libSCAnd.so` and `libOpenAL64.so` from the local developer files and must pass the default `tools/audit_android_apk.py` audit. Do not deliver a `-PnoGameFiles` build as a drop-in replacement: the 397-file CDN manifest contains game data, not those engine libraries. Asset synchronization therefore reports data verification separately from engine readiness.

Release/explicit `-PnoGameFiles` builds retain the existing external-library installation policy. Audit those variants with `python3 tools/audit_android_apk.py <apk> --external-game-libraries`. Both forms require the launcher library and audit every packaged native library's ARM64 ABI and 16 KB alignment.

Interactive Discord/browser/Keystore checks on a phone remain necessary; no device was available over ADB during this implementation.
