# Pairing codes

A **pairing code** is a single opaque string the streamer pastes into the OBS
plugin's **Pairing code** field. It fills in the relay URL, stream ID,
broadcaster token, listener token, channels, and bitrate so the streamer never
types six fields. It is the primary, idiot-proof setup path.

The control panel / website (`alerts.irlhosting.com`) generates these codes when
it provisions a stream. The plugin only ever **decodes** them.

## Format

```
base64url( "IAR1|<relay_url>|<stream_id>|<broadcaster_token>|<listener_token>|<channels>|<bitrate_bps>" )
```

- Field separator is `|`. Tokens are high-entropy and must not contain `|`.
- `channels` is `1` or `2`; `bitrate_bps` is one of 24000/32000/48000/64000/96000.
- The encoding is **base64url** (`-`/`_`, padding optional).
- An optional `IAR1:` prefix on the final string is accepted and ignored, so you
  may hand out either `eyJ...` or `IAR1:eyJ...`.

The reference encoder/decoder is in `obs-plugin/src/core/settings.cpp`
(`BuildPairingCode` / `ParsePairingCode`) and is unit tested in
`obs-plugin/test/core_tests.cpp`.

## Example generator (Node / TypeScript)

This is what the control panel will use when issuing a stream:

```ts
function buildPairingCode(p: {
  relayUrl: string; streamId: string; broadcasterToken: string;
  listenerToken: string; channels: 1 | 2; bitrateBps: number;
}): string {
  const raw = ["IAR1", p.relayUrl, p.streamId, p.broadcasterToken,
               p.listenerToken, String(p.channels), String(p.bitrateBps)].join("|");
  return Buffer.from(raw, "utf8").toString("base64url");
}
```

## What the streamer still does manually

A pairing code cannot know which OBS source carries the alerts, so the streamer
must route that source to **Track 6** in Advanced Audio Properties once. The
plugin defaults the Track selector to 6. Everything else is auto.

## Security notes

- A pairing code embeds the **broadcaster token** (a secret). Treat the code
  itself as a secret: deliver it over an authenticated channel (the panel after
  login), not a public page.
- The plugin masks the token after the code is applied and never logs it.
- Rotating the broadcaster token invalidates old codes; issue a new code.
