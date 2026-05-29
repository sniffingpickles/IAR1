# Troubleshooting

Start with the dock's **Status** panel (State + Last error) and the
**Export diagnostic bundle** button (secrets redacted). On the relay, check
`journalctl -u iar-relay` and `GET /statusz`.

## Connection states (plugin)

| State | Meaning | What to check |
|-------|---------|---------------|
| Disconnected | Not started / stopped | Click Start. |
| Connecting | TLS + WS handshake + waiting for `welcome` | Relay URL reachable? DNS/firewall? |
| Connected | Authenticated, sending audio | All good. |
| Reconnecting | Lost connection, retrying | Network blip; auto-reconnects if enabled. |
| Auth failed | Relay rejected token/params | Wrong token, stream ID, channels or sample rate. |
| Relay unreachable | Could not connect | URL, DNS, TLS cert, firewall, relay down. |

## "Auth failed" immediately after Start

- **Wrong stream key/token** for that stream ID.
- **Stream ID mismatch** - must match a `streams[].id` in the relay config.
- **Channels mismatch** - the plugin's Mono/Stereo must equal the stream's
  `channels` in the relay config (the track is fixed per stream).
- **Sample rate mismatch** - keep both at 48000 unless you changed the stream.
- The relay logs `broadcaster auth rejected` with a code; it never logs the token.

## "Relay unreachable"

- `curl https://your-relay/healthz` should return `ok`.
- Check the WSS path is exactly `/contribute`.
- Behind Nginx, ensure WebSocket upgrade headers are proxied.
- TLS certificate must be valid (the plugin does not trust self-signed by default).

## Listener can't connect / no audio

1. **Broadcaster offline** - the listener page shows this and the relay returns
   `409` to offers. Start the plugin first; listeners auto-retry.
2. **Tap to Start not pressed** - iOS requires the gesture; audio cannot autoplay.
3. **WebRTC blocked** - the most common field issue is the relay's media UDP not
   being reachable:
   - Set `webrtc.nat_1to1_ip` to the relay's public IP.
   - Open `webrtc.udp_port_min..max` in the OS and cloud firewall.
   - On restrictive cellular/corporate networks, add a **TURN** server to
     `ice_servers`.
4. **Wrong/expired link** - the page shows "invalid or revoked". Re-issue the
   listener token.
5. **Password required** - the page prompts for the PIN if the stream sets one.

## "No audio for 10s+" warning

- The selected OBS track is silent. Open **Advanced Audio Properties** and confirm
  the source actually feeds the track you selected (Track 6 = mix index 5).
- Use **Test tone (1s)** to push a beep; if listeners hear the beep, the path is
  fine and the issue is your OBS routing.

## Dropped audio frames / ring buffer fill rising

- Indicates network congestion or a stalled worker. The plugin drops the *oldest*
  queued Opus packets to protect latency and never stalls OBS.
- Check uplink bandwidth and the relay's reachability. Lower the bitrate (e.g.
  Mono 24-32 kbps) if on a constrained connection.

## Listener hears choppy audio

- High packet loss on the listener's network. Opus in-band FEC is enabled, but
  severe loss still degrades. Try a different network or a closer relay region.
- Verify the relay isn't CPU-starved (`/metrics`, `top`). The relay does not
  transcode, so CPU should be low even with many listeners.

## Plugin doesn't appear in OBS

- Confirm OBS version >= 30 and the architecture matches (universal on macOS).
- macOS quarantine: `xattr -dr com.apple.quarantine irl-audio-return.plugin`.
- Check the OBS log (`Help > Log Files`) for `irl-audio-return loaded`.

## Build issues

- **macOS local build, `framework 'AGL' not found`:** very new SDKs dropped AGL.
  The plugin's CMake already redirects Qt's WrapOpenGL fallback to OpenGL; ensure
  you reconfigured after pulling.
- **Core tests can't find Opus:** install `libopus-dev`/`opus` and `pkg-config`,
  or use the vcpkg toolchain on Windows.
- See `obs-plugin/README.md` for the full build matrix.
