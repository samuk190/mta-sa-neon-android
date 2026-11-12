# MTA Neon Network Protocol Reference

> Authoritative reference for Android reimplementation. Derived from `mta_neon_source/` desktop client headers.

---

## 1. Packet ID Enumeration (eRakNetPackets)

All packet IDs are `uint8_t` values written to the BitStream as the first byte.

### Internal RakNet Packets (0–13)
| ID | Name | Direction | Description |
|----|------|-----------|-------------|
| 0 | RID_INTERNAL_PING | Bidirectional | Connected system ping (internal only) |
| 1 | RID_PING | Bidirectional | Unconnected system ping |
| 2 | RID_PING_OPEN_CONNECTIONS | Server→Client | Ping only if open connections exist |
| 3 | RID_CONNECTED_PONG | Bidirectional | Pong response, updates timestamps |
| 4 | RID_CONNECTION_REQUEST | Client→Server | New connection request |
| 5 | RID_SECURED_CONNECTION_RESPONSE | Server→Client | Secured connection challenge |
| 6 | RID_SECURED_CONNECTION_CONFIRMATION | Client→Server | Secured connection confirmation |
| 7 | RID_RPC_MAPPING | Bidirectional | Integer-to-name RPC mapping |
| 8 | RID_DETECT_LOST_CONNECTIONS | Bidirectional | Reliable lost-connection detection |
| 9 | RID_OPEN_CONNECTION_REQUEST | Client→Server | Offline message to start new connection |
| 10 | RID_OPEN_CONNECTION_REPLY | Server→Client | Offline reply confirming connection reset |
| 11 | RID_RPC | Bidirectional | Remote procedure call |
| 12 | RID_RPC_REPLY | Bidirectional | RPC return data |
| 13 | RID_OUT_OF_BAND_INTERNAL | Bidirectional | Internal OOB (NAT punchthrough port advertisement) |

### User-Facing Connection Status Packets (14–27)
| ID | Name | Direction | Description |
|----|------|-----------|-------------|
| 14 | RID_CONNECTION_REQUEST_ACCEPTED | Server→Client | Connection accepted by server |
| 15 | RID_CONNECTION_ATTEMPT_FAILED | Local | Cannot connect to target |
| 16 | RID_ALREADY_CONNECTED | Local | Already connected to this system |
| 17 | RID_NEW_INCOMING_CONNECTION | Server | Remote system connected successfully |
| 18 | RID_NO_FREE_INCOMING_CONNECTIONS | Server→Client | Server full |
| 19 | RID_DISCONNECTION_NOTIFICATION | Bidirectional | Graceful disconnect |
| 20 | RID_CONNECTION_LOST | Local | Reliable delivery failed, connection closed |
| 21 | RID_RSA_PUBLIC_KEY_MISMATCH | Local | RSA key mismatch with remote |
| 22 | RID_CONNECTION_BANNED | Server→Client | Banned from server |
| 23 | RID_INVALID_PASSWORD | Server→Client | Wrong password |
| 24 | RID_MODIFIED_PACKET | Local | Packet tampered in transit |
| 25 | RID_TIMESTAMP | Bidirectional | System time delta (requires SetOccasionalPing) |
| 26 | RID_PONG | Server→Client | Unconnected pong with ping + enum data |
| 27 | RID_ADVERTISE_SYSTEM | Bidirectional | IP/Port advertisement + offline data |

### Plugin Packets (28–113)
| Range | Plugin | Key IDs |
|-------|--------|---------|
| 28–30 | ConnectionGraph | REMOTE_DISCONNECTION, REMOTE_CONNECTION_LOST, REMOTE_NEW_INCOMING |
| 31 | FileTransfer | DOWNLOAD_PROGRESS |
| 32–34 | FileListTransfer | HEADER, FILE, REFERENCE_PUSH_ACK |
| 35 | DirectoryDeltaTransfer | DDT_DOWNLOAD_REQUEST |
| 36 | RakNetTransport | TRANSPORT_STRING |
| 37–42 | ReplicaManager | CONSTRUCTION, DESTRUCTION, SCOPE_CHANGE, SERIALIZE, DOWNLOAD_STARTED, DOWNLOAD_COMPLETE |
| 43–48 | ConnectionGraph2 | REQUEST, REPLY, UPDATE, NEW_CONNECTION, CONNECTION_LOST, DISCONNECTION |
| 49 | Router | ROUTE_AND_MULTICAST |
| 50–53 | RakVoice | OPEN_CHANNEL_REQUEST/REPLY, CLOSE_CHANNEL, DATA |
| 54–62 | Autopatcher | GET_CHANGELIST, CREATION_LIST, DELETION_LIST, GET_PATCH, PATCH_LIST, REPOSITORY_FATAL_ERROR, FINISHED_INTERNAL, FINISHED, RESTART_APPLICATION |
| 63–72 | NATPunchthrough | REQUEST, CONNECT_AT_TIME, GET_MOST_RECENT_PORT, CLIENT_READY, TARGET_NOT_CONNECTED, TARGET_UNRESPONSIVE, CONNECTION_TO_TARGET_LOST, ALREADY_IN_PROGRESS, PUNCHTHROUGH_FAILED, PUNCHTHROUGH_SUCCEEDED |
| 73–78 | LightweightDatabase | QUERY_REQUEST, UPDATE_ROW, REMOVE_ROW, QUERY_REPLY, UNKNOWN_TABLE, INCORRECT_PASSWORD |
| 79–83 | ReadyEvent | SET, UNSET, ALL_SET, QUERY, FORCE_ALL_SET |
| 84 | Lobby | LOBBY_GENERAL |
| 85–89 | AutoRPC | CALL, REMOTE_INDEX, UNKNOWN_REMOTE_INDEX, REMOTE_ERROR |
| 90 | FileListTransfer | FILE_LIST_REFERENCE_PUSH |
| 91–93 | Rooms | EXECUTE_FUNC, LOGON_STATUS, HANDLE_CHANGE |
| 94–95 | Lobby2 | SEND_MESSAGE, SERVER_ERROR |

### Protocol Version & Mesh
| ID | Name | Description |
|----|------|-------------|
| 96 | RID_INCOMPATIBLE_PROTOCOL_VERSION | RAKNET_PROTOCOL_VERSION mismatch (byte[1] = remote version) |
| 97 | RID_FCM2_ELAPSED_RUNTIME | FullyConnectedMesh2 internal |
| 98 | RID_FCM2_NEW_HOST | New host notification (systemAddress + GUID) |
| 99 | RID_UDP_PROXY_GENERAL | UDP proxy message (byte[1] = type) |

### User Packet Boundary
| ID | Name | Description |
|----|------|-------------|
| **100** | **RID_USER_PACKET_ENUM** | **MTA game packets start here (cast to uint8_t before writing)** |

> **Critical**: All eRakNetPackets values must be cast to `(unsigned char)` before writing to RakNet::BitStream per the header comment. Values above 255 (like RID_INCOMPATIBLE_PROTOCOL_VERSION = 270 in some forks) indicate a RakNet version mismatch — verify against your server's actual RakNet revision.

---

## 2. Connection Handshake Sequence

Based on `CConnectManager::Connect()` and `CNet` interface:

```
Client                              Server
  │                                    │
  │──── RID_OPEN_CONNECTION_REQUEST ──▶│  (Offline message, triggers connection reset)
  │◀── RID_OPEN_CONNECTION_REPLY ─────│  (Confirms server is ready)
  │                                    │
  │──── RID_CONNECTION_REQUEST ───────▶│  (Formal connection with credentials)
  │    [serial, nickname, password]     │
  │                                    │
  │◀── RID_SECURED_CONNECTION_RESPONSE │  (Optional: RSA/secured challenge)
  │──── RID_SECURED_CONNECTION_CONFIRM ▶│  (Optional: challenge response)
  │                                    │
  │◀── RID_CONNECTION_REQUEST_ACCEPTED │  (Success) OR
  │◀── RID_CONNECTION_BANNED           │  (Banned) OR
  │◀── RID_INVALID_PASSWORD            │  (Wrong password) OR
  │◀── RID_NO_FREE_INCOMING_CONNECTIONS│  (Server full) OR
  │    RID_CONNECTION_ATTEMPT_FAILED   │  (Local failure, no server response)
  │                                    │
  │════ Game session active ═══════════│
  │                                    │
  │──── RID_DISCONNECTION_NOTIFICATION ▶│  (Graceful quit) OR
  │    RID_CONNECTION_LOST             │  (Timeout/unreachable)
```

### Handshake Validation Steps (Client-Side)
Per `CConnectManager::Connect()`:
1. **Network readiness check**: `CCore::IsNetworkReady()` must return true
2. **Host validation**: Parse IPv4 address, reject non-resolvable endpoints
3. **Native world startup endpoint validation**: `ValidateNativeWorldStartupEndpoint()` checks pinned connection rules
4. **Nickname validation**: `CheckNickProvided()` rejects empty/invalid nicks
5. **Disk space check**: `CheckDiskSpace()` ensures room for resource downloads
6. **Active session drain**: If a native world session is active, queue the connection and wait for ordered drain before proceeding

---

## 3. Packet Priority & Reliability

### NetPacketPriority (from net_common.h)
| Value | Enum | Usage |
|-------|------|-------|
| 0 | PACKET_PRIORITY_HIGH | Time-critical: sync, chat, voice |
| 1 | PACKET_PRIORITY_MEDIUM | Normal game data |
| 2 | PACKET_PRIORITY_LOW | Background: file transfers, statistics |

### NetPacketReliability (from net_common.h)
| Value | Enum | Behavior |
|-------|------|----------|
| 0 | PACKET_RELIABILITY_UNRELIABLE | May arrive out of order, may be lost |
| 1 | PACKET_RELIABILITY_UNRELIABLE_SEQUENCED | Out-of-order packets discarded, may be lost |
| 2 | PACKET_RELIABILITY_RELIABLE | Guaranteed delivery, may arrive out of order |
| 3 | PACKET_RELIABILITY_RELIABLE_ORDERED | Guaranteed delivery in order |
| 4 | PACKET_RELIABILITY_RELIABLE_SEQUENCED | Guaranteed delivery, out-of-order discarded |

### ePacketOrdering Channels
| Value | Enum | Purpose |
|-------|------|---------|
| 0 | PACKET_ORDERING_DEFAULT | General game data |
| 1 | PACKET_ORDERING_CHAT | Chat messages (separate ordering channel) |
| 2 | PACKET_ORDERING_DATA_TRANSFER | File/resource downloads |
| 3 | PACKET_ORDERING_VOICE | Voice chat data |
| 4 | PACKET_ORDERING_NATIVE_TASK_PRESENTATION | Native world task sync |

---

## 4. BitStream Serialization Format

### Endianness
- **Little-endian** for all multi-byte integer and float types (x86/ARM compatible)
- Bit-level packing follows RakNet's standard: bits are packed LSB-first within each byte

### Type Serialization
| Type | Size | Notes |
|------|------|-------|
| uint8_t / bool | 8 bits | Single byte |
| uint16_t | 16 bits | Little-endian |
| uint32_t / int32_t | 32 bits | Little-endian |
| float | 32 bits | IEEE 754 little-endian |
| string | Variable | Length-prefixed: uint16 length + raw bytes (no null terminator in stream) |
| Compressed float | Variable | RakNet WriteCompressed/ReadCompressed for normalized [-1,1] or [0,1] ranges |
| Vector3 | 96 bits | Three compressed floats (x, y, z) |
| Quaternion | Variable | Compressed rotation representation |

### String Encoding
- **ASCII/Latin-1 only** in network packets
- UTF-8 strings must be normalized to ASCII before serialization (per AGENTS.md directive 7)
- Maximum string length: typically 512 bytes for chat, 256 for nicknames

---

## 5. Network Statistics Structure

From `net_common.h` `NetStatistics`:

```cpp
struct NetStatistics {
    unsigned long long bytesReceived;
    unsigned long long bytesSent;
    uint               packetsReceived;
    uint               packetsSent;
    float              packetlossTotal;
    float              packetlossLastSecond;
    uint               messagesInSendBuffer;
    uint               messagesInResendBuffer;
    bool               isLimitedByCongestionControl;
    bool               isLimitedByOutgoingBandwidthLimit;
    NetRawStatistics   raw;  // Full RakNet stats copy
};
```

`NetRawStatistics` contains detailed per-priority counters for sent/received messages, ACKs, resends, splits, CRC errors, and bandwidth metrics.

---

## 6. CNet Interface Contract (Methods Required for Android)

From `sdk/net/CNet.h`, the Android implementation must provide:

| Method | Purpose |
|--------|---------|
| `StartNetwork(host, port, packetTag)` | Initialize connection to server |
| `StopNetwork()` | Tear down connection |
| `DoPulse()` | Per-frame network tick (call every frame) |
| `Shutdown()` | Full cleanup |
| `RegisterPacketHandler(handler)` | Set callback for incoming packets |
| `AllocateNetBitStream()` / `DeallocateNetBitStream()` | BitStream pool management |
| `SendPacket(id, bitstream, priority, reliability, ordering)` | Send game packet |
| `GetNetworkStatistics(stats)` | Query traffic stats |
| `GetPing()` | Current RTT in ms |
| `GetTime()` | RakNet timestamp |
| `GetSerial(buffer, maxLen)` | Client serial/key identifier |
| `VerifySignature(data, size)` | Validate server signature |
| `SetTimeoutTime(ms)` | Connection timeout |
| `Reset()` | Reset network state without full shutdown |
| `GetHTTPDownloadManager(mode)` | Resource/file download subsystem |
| `SetServerBitStreamVersion(ver)` | Negotiate serialization version |
| `GetConnectionError()` / `SetConnectionError()` | Error state management |

---

## 7. Gap Analysis: Existing Mobile vs. Desktop Protocol

The previous `descompile_mta_launcher` mobile implementation had these gaps (per exploration report):

| Feature | Desktop | Old Mobile | Status in New Implementation |
|---------|---------|------------|------------------------------|
| RakNet session management | Full | Raw POSIX UDP only | ✅ Implemented via RakNetWrapper |
| ACK/reliability layer | Full | Missing | ✅ Implemented via RakNetWrapper |
| Connection handshake | Full 4-step | Basic JOIN only | ⚠️ Stubs present, needs server validation |
| BitStream compatibility | Canonical | Custom impl | ✅ Matching format in RakNetWrapper |
| Packet statistics | Full NetRawStatistics | None | ✅ Per-ID stats in CNetAdapter |
| HTTP download manager | Full | Missing | ❌ Not yet implemented |
| Binary file validation | CheckFile/EncryptDumpfile | Missing | ❌ Not yet implemented |
| Script deobfuscation | DeobfuscateScript | Missing | ❌ Not yet implemented |
| Serial/signature verification | VerifySignature | Missing | ⚠️ Stub present |
| Fake lag simulation | SetFakeLag | Missing | No-op on mobile (acceptable) |
| NAT punchthrough | Full plugin set | Missing | ❌ Not needed for direct-connect |

---

## 8. Critical Integration Notes

1. **RakNet Version**: The exact RakNet revision used by your MTA Neon server MUST match the client. Extract from `netc.dll` symbols or server logs. Using the wrong version causes silent packet drops or INCOMPATIBLE_PROTOCOL_VERSION (ID 96).

2. **Packet ID Casting**: Always cast eRakNetPackets to `(unsigned char)` before BitStream write. Values > 255 indicate version mismatch.

3. **String Sanitization**: Every string written to the network MUST pass through `NormalizeAscii()` to prevent mojibake on the RenderWare engine.

4. **Thread Safety**: `DoPulse()` must be called from the game/main thread only. RakNetWrapper handles internal threading; the adapter is single-threaded from the game loop perspective.

5. **BitStream Pooling**: Use `AllocateNetBitStream()` / `DeallocateNetBitStream()` instead of stack-allocating BitStreams to reduce GC pressure on mobile.

6. **16KB Page Alignment**: All native libraries must use `-Wl,-z,max-page-size=16384` for Android 15+ compatibility. Already configured in CMakeLists.txt.