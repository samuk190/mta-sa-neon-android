# Neon Android Transport Diagnostics

**Audit Date:** 2026-11-10.
The Android APK on the physical device reached the connection attempt stage using vendored RakNet, but timed out before receiving `MOD_NAME`. This document details the root causes, protocol divergence, and the native MTA transport reimplementation.

---

## 1. Observed Evidence

| Source | Observation | Impact |
|---|---|---|
| Device logs (`boot1.log`), 21:59 | `vendor packet ID 17 translated to RID 15`, followed by `CONNECTION_ATTEMPT_FAILED` and `NeonJoin` timeout | RakNet connection attempt failed before completing handshake |
| `MessageIdentifiers.h`, lines 111–133 | Vendor `ID_CONNECTION_ATTEMPT_FAILED = 17`; `ID_INCOMPATIBLE_PROTOCOL_VERSION = 25` | Packet ID 17 signifies a local connection failure event |
| `RakPeer.cpp`, lines 5720–5727 | RakPeer enqueues `ID_CONNECTION_ATTEMPT_FAILED` internally when retries are exhausted | This event is generated locally by the client, without explicit server rejection |
| `RakPeer.cpp`, lines 5758–5761 | Opening packet ID `0x05`, 16-byte magic, protocol version, and MTU padding | Standard RakNet format emitted by stock RakNet library |
| Root `net.so` binary | AArch64 Linux ELF with strings `CCryptRC4.hpp` and `ReliabilityLayer4.cpp` | Indicates MTA-specific custom network transport layer |
| Prior Mobile handshake captures | Earlier client sent a 51-byte opening datagram, parsed response, activated RC4, sent connection request, and received CRA `0x0E`, followed by `MOD_NAME 0x6A` | Proven handshake sequence compatible with the server |

The standard RakNet library translates packet IDs after `RakPeerInterface::Receive()`. However, stock RakNet opening datagrams (using standard RakNet magic) are ignored by the MTA server's `net.so` module.

---

## 2. Reproducible Probe Script

`tests/probe_neon_transport.py` is a standalone Python 3 utility using standard libraries. It sends an ASE query and six RakNet opening variants (versions 5/6 and MTUs 1492/1200/576) over individual sockets with configured timeouts.

### Execution:
```bash
python3 tests/probe_neon_transport.py --host worldrpgbroficial.duckdns.org --timeout 1.5
```

### Remote VM Execution (Oracle Cloud):
```bash
ssh -o BatchMode=yes -i ~/.ssh/oracle_worldrpg_rsa ubuntu@146.235.24.222 \
    'python3 - --host worldrpgbroficial.duckdns.org --timeout 1.5' \
    < tests/probe_neon_transport.py
```

### Findings:
1. **ASE Query (`EYE1`)**: Successfully received a 183-byte response, verifying that the server port is open and answering ASE queries.
2. **Stock RakNet OPEN1**: Emitted timeouts (`no_response_within_timeout`) on all MTU variants. The server discarded the packets because it requires the 51-byte MTA opening handshake rather than stock RakNet.

---

## 3. Native MTA Transport Implementation

To establish a reliable connection with the MTA Neon server:

1. **Custom 51-byte Opening Datagram**:
   - Header: `0x09 0x04`
   - Client Nonce (4 bytes)
   - Protocol Magic: `00 ff 00 ff fe fe fe fe fd fd fd fd 78 56 34 17`
   - MTU/Version Offer and Tail Byte (`0xDE`)
2. **RC4 Stream Cipher Activation**:
   - Dynamic key derivation based on client/server nonces.
   - Stream cipher applied to payload after handshake verification.
3. **Connection Handshake**:
   - Client sends Connection Request (`0x04`).
   - Server returns Connection Request Accepted (`0x0E`).
   - Server sends `MOD_NAME` (`0x6A`), triggering resource and game session initialization.
4. **Integration with `MtaClientTransport`**:
   - `MtaClientTransport.cpp` replaces `RakNetDirectTransport.cpp` and `RakNetAdapter.cpp`.
   - Propagates real connection status and error codes directly to `GameActivity` and `EngineHostActivity`.
