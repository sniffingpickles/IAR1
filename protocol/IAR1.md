# IAR1 - IRL Audio Return contribution protocol, version 1

IAR1 is the wire protocol used by the **OBS plugin (broadcaster)** to push a single
Opus audio stream to the **relay** over a single secure WebSocket (WSS) connection.

It is intentionally small and one-directional for media:

- **Control** uses UTF-8 **JSON text frames**.
- **Media** uses **binary frames** carrying a fixed header + one Opus packet.

The relay never decodes or re-encodes audio. It validates, then fans the exact
Opus payloads out to WebRTC listeners as Opus RTP.

---

## 1. Transport

- WebSocket over TLS (`wss://`) in production. Plain `ws://` is only for loopback dev.
- Endpoint: `GET {relay}/contribute` with `Upgrade: websocket`.
- Subprotocol header: `Sec-WebSocket-Protocol: iar1`.
- One broadcaster connection per stream. A new authenticated connection for the
  same stream **replaces** the old one (broadcaster reconnect keeps the same
  listener URLs).
- Max control frame size: **16 KiB**. Max binary frame size: **8 KiB**
  (an Opus packet for 20 ms stereo at 64 kbps is ~160 bytes; 8 KiB is generous).

## 2. Connection lifecycle

```
Plugin                              Relay
  | --- WS upgrade (proto iar1) --->  |
  | ---------- HELLO (json) -------->  |     (auth: token + stream params)
  |  <--------- WELCOME (json) ------  |     (or ERROR then close)
  | ====== MEDIA (binary) x N ======> |
  | --------- STATS (json) -------->   |     (optional, periodic)
  |  <--------- STATUS (json) ------   |     (periodic: listeners, etc.)
  | ----------- BYE (json) -------->   |     (graceful)
```

If `WELCOME` is not received within **10 s** of `HELLO`, the plugin closes and
reconnects with backoff.

## 3. Control messages (JSON text frames)

Every control message is a JSON object with a `"t"` (type) field.

### 3.1 `hello` (plugin -> relay), first frame after upgrade

```json
{
  "t": "hello",
  "v": 1,
  "stream_id": "alerts-main",
  "token": "BROADCASTER_TOKEN",
  "codec": "opus",
  "sample_rate": 48000,
  "channels": 1,
  "frame_ms": 20,
  "bitrate": 32000,
  "client": "irl-audio-return-plugin/1.0.0 (obs; macos)"
}
```

| Field         | Type   | Notes |
|---------------|--------|-------|
| `t`           | string | `"hello"` |
| `v`           | int    | Protocol version. Must be `1`. |
| `stream_id`   | string | `[a-z0-9][a-z0-9-_]{0,63}`. Identifies the stream/registry slot. |
| `token`       | string | Broadcaster token. **Never** logged in plaintext. |
| `codec`       | string | `"opus"` only in v1. |
| `sample_rate` | int    | Opus clock rate. `48000` recommended (also 8000/12000/16000/24000). |
| `channels`    | int    | `1` or `2`. |
| `frame_ms`    | int    | `10` or `20`. Informational; affects RTP packetization timing. |
| `bitrate`     | int    | Target encoder bitrate in bps. Informational. |
| `client`      | string | Free-form client/version string. |

### 3.2 `welcome` (relay -> plugin)

```json
{ "t": "welcome", "v": 1, "stream_id": "alerts-main",
  "session": "f3c9...", "server": "iar-relay/1.0.0", "heartbeat_ms": 15000 }
```

The plugin must send a WebSocket **ping** (or a `ping` control message) at least
every `heartbeat_ms`. The relay drops idle broadcasters after `2 × heartbeat_ms`.

### 3.3 `status` (relay -> plugin), periodic

```json
{ "t": "status", "listeners": 2, "uptime_s": 412, "bytes_in": 9123456 }
```

### 3.4 `stats` (plugin -> relay), optional periodic

```json
{ "t": "stats", "seq": 12000, "dropped": 3, "ring_fill": 0.12, "kbps_out": 33.1 }
```

### 3.5 `ping` / `pong`

```json
{ "t": "ping", "ts": 1716950000000 }
{ "t": "pong", "ts": 1716950000000 }
```

WebSocket-level ping/pong is also accepted and preferred.

### 3.6 `bye` (either direction)

```json
{ "t": "bye", "reason": "shutdown" }
```

### 3.7 `error` (relay -> plugin), then close

```json
{ "t": "error", "code": "auth_failed", "message": "invalid token" }
```

| `code`            | Meaning |
|-------------------|---------|
| `bad_request`     | Malformed `hello` / missing fields. |
| `unsupported`     | Bad version or codec. |
| `auth_failed`     | Token rejected. |
| `rate_limited`    | Too many auth attempts; retry later. |
| `stream_busy`     | Server policy refused replacing the active broadcaster. |
| `server_error`    | Internal error. |

The relay never echoes the token back in any message or log.

## 4. Media frames (binary)

All multi-byte integers are **big-endian (network byte order)**.

```
 0               1               2               3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|   'I'    |   'A'    |   'R'    |   '1'    |   magic (4 bytes)  |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| version  |  flags   |        header_len (uint16)             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                  sequence number (uint64)                     |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|              timestamp, RTP-style samples (uint64)            |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|        duration_samples (uint32)        |  payload_size u16   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    payload (Opus packet)  ...                  |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

### 4.1 Header layout (30 bytes fixed in v1)

| Offset | Size | Field              | Notes |
|-------:|-----:|--------------------|-------|
| 0      | 4    | `magic`            | ASCII `IAR1` (`0x49 0x41 0x52 0x31`). |
| 4      | 1    | `version`          | `1`. |
| 5      | 1    | `flags`            | bit0 `KEY` (Opus always decodable; set to 1), bit1 `MARKER` (start/discontinuity), others reserved 0. |
| 6      | 2    | `header_len`       | Total header bytes (=`30` in v1). Always points at the first payload byte; allows forward-compatible growth. |
| 8      | 8    | `seq`              | Monotonic per connection, starts at 0. |
| 16     | 8    | `timestamp`        | Cumulative sample count at the codec sample rate (RTP-style). |
| 24     | 4    | `duration_samples` | Samples in this frame (e.g. 960 for 20 ms @ 48 kHz). |
| 28     | 2    | `payload_size`     | Opus payload byte length; must equal `frameLen - header_len`. |

> Wire order: `magic(4) version(1) flags(1) header_len(2) seq(8) timestamp(8)
> duration_samples(4) payload_size(2)` = **30 bytes**, then the payload. The
> reference encoder/decoder in `obs-plugin/src/core/iar_packet.*` and
> `relay/internal/contrib/packet.go` are authoritative and are tested to
> round-trip against each other.

### 4.2 Validation rules (relay)

A media frame is **dropped** (not fatal) if any of:

- total frame length `< header_len`.
- `magic != "IAR1"` or `version != 1`.
- `header_len < 30` or `header_len >` frame length.
- `payload_size != frameLen - header_len`.
- `payload_size == 0` or `payload_size > 8192`.
- `duration_samples == 0` or `> sample_rate` (sanity: > 1 s of audio).

Malformed control frames close the connection with an `error`.

### 4.3 Sequence & timestamp

- `seq` increments by exactly 1 per emitted media frame.
- `timestamp` increments by `duration_samples` per frame.
- After broadcaster reconnect both reset; the relay treats a new connection as a
  fresh RTP source (new SSRC) for listeners.

## 5. Relay -> WebRTC mapping

The relay creates one Opus RTP track per stream. For each valid IAR1 media frame
it writes an RTP packet whose payload is the **unmodified** Opus packet, with
`Duration = duration_samples / (sample_rate/1000) ms`. No transcoding occurs.
Listener negotiation advertises Opus with the matching clock rate and channel
count from `hello`.

## 6. Versioning

`version`/`v` is bumped only for incompatible header or control-shape changes.
New optional control fields and new `flags` bits are backward compatible. Clients
must ignore unknown JSON fields and unknown `flags` bits.
