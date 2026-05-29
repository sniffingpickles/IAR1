# OBS setup - IRL Audio Return

This guide covers installing the plugin and routing audio so your phone receives
only the private return audio (alerts/TTS/Discord), not your whole stream.

## Compatibility

- OBS Studio **30.0+** (developed and tested against OBS 31/32).
- macOS 12+ (universal: Apple Silicon + Intel). Windows x64 via CI builds.

## Install the plugin

### macOS

1. Download `irl-audio-return.plugin` (from a release or CI artifact).
2. Quit OBS.
3. Copy the bundle to:
   ```
   ~/Library/Application Support/obs-studio/plugins/irl-audio-return/Contents/...
   ```
   The simplest path: place `irl-audio-return.plugin` into
   `~/Library/Application Support/obs-studio/plugins/` if your OBS build accepts
   single-bundle plugins, otherwise use the packaged `.pkg` installer which puts
   files in the correct location automatically.
4. If macOS quarantines it: `xattr -dr com.apple.quarantine irl-audio-return.plugin`.
5. Start OBS. Open **Docks > IRL Audio Return** (or it appears automatically).

### Windows

1. Download the Windows build artifact.
2. Copy `obs-plugins/64bit/irl-audio-return.dll` and the
   `data/obs-plugins/irl-audio-return/` folder into your OBS install directory
   (e.g. `C:\Program Files\obs-studio\`), preserving that structure.
3. Start OBS and open the **IRL Audio Return** dock.

## Route audio to a dedicated track

The whole point is to send a *private* track. Use Advanced Audio Properties to
put return audio on a track you do **not** send to your public platform.

1. Add or choose your alert/TTS/browser/Discord audio source.
2. **Audio Mixer > the gear/menu icon on any source > Advanced Audio Properties.**
3. In the **Tracks** columns for each source, tick the tracks it should feed:
   - Public stream audio (mic, game, music) > **Track 1**.
   - Private return audio (alerts, TTS, Discord/mod voice) > **Track 6**.
4. Make sure your streaming encoder uses **Track 1** (Settings > Output >
   Streaming > Audio Track = 1). This keeps Track 6 private.

> The plugin maps the UI's **Track N** to OBS mix index **N-1** internally.
> Track 6 = mix index 5.

## Get your link (no account)

Open **https://alerts.irlhosting.com**, click **Create my link**, and you'll get:

- a **pairing code** (for the OBS plugin), and
- a **listener URL** (for your phone / Moblin).

Keep both private.

## The dock vs. settings

The **IRL Audio Return dock** is a slim, glance-while-live panel: connection
state, listener count, outbound bitrate, dropped frames, audio activity, a big
**Start/Stop** button, and an **“Start automatically when OBS launches”** toggle.

All configuration lives in **Settings...** (button on the dock, or
**Tools > IRL Audio Return settings...**).

## Configure (one paste)

1. Open **Settings...** and paste your **pairing code** into the *Pairing code*
   field - it fills the relay URL, stream ID, tokens, channels, and bitrate.
2. Pick the **OBS Track** you routed return audio to (defaults to **Track 6**),
   then **Save**.
3. Back in the dock, tick **Start automatically when OBS launches**, then click
   **Start** once.

From then on it **just streams** on every OBS launch - no clicks.

### Manual fields (Settings... dialog, if you're not using a pairing code)

| Field | Value |
|-------|-------|
| Relay URL | `wss://alerts.irlhosting.com/contribute` (pre-filled) |
| Stream ID | matches a stream on the relay, e.g. `alerts-main` |
| Stream key / token | the broadcaster token for that stream |
| Listener token (optional) | lets the dock build a copyable listener URL |
| OBS Track | **Track 6** (or wherever your return audio is routed) |
| Bitrate / Channels / Frame | Mono / 32 kbps / 20 ms (defaults); Stereo / 64 kbps for music |

Watch the **Status** panel: Connecting > Connected, with listener count, outbound
bitrate, dropped frames, and ring-buffer fill. Use **Test tone (1s)** to push a
440 Hz beep through the whole path.

> The token is never shown after saving (masked, length hidden). Clicking **Stop**
> disables auto-start; the next paste/Start re-enables it.

## Playback on the phone - Moblin (recommended for IRL)

You can hear the return audio in your earpiece directly inside the **Moblin** IRL
app using its **Browser widget**, with no extra app and no tapping:

1. In Moblin: **Settings > Scenes > (your scene) > Add widget > Browser**.
2. Set the **URL** to your listener link: `https://alerts.irlhosting.com/l/<listener_token>`.
3. Set the widget **Mode** to **Audio only**.
4. Done - the page auto-connects and auto-plays. When you go live and the OBS
   plugin is streaming, you'll hear the return audio. It auto-reconnects if the
   connection drops.

> The listener page is built to autoplay in a browser source (Moblin/OBS), so no
> "tap to start" is needed there. Opened by hand in mobile Safari it will show a
> one-time **Tap to enable audio** button (iOS autoplay rule).

### One-tap Moblin setup (QR)

After **Create my link**, the page shows an **"Add to Moblin"** QR code. Scan it
with your phone's camera: it opens Moblin via a `moblin://` deep link and points
Moblin's built-in **Web browser** at your return-audio page. Open that Web
browser in Moblin to listen.

> Moblin's deep-link import covers streams / quick buttons / the Web browser /
> remote control - **not** scene widgets - so the QR configures the Web browser.
> For audio composited into a scene instead, add a **Browser widget** (Audio
> only) with the listener URL manually as above.

You can also just open the listener URL in **Safari** (or add it to the Home
Screen) if you prefer a dedicated page.

## Streamer workflow (end to end, minimal interaction)

One-time setup:

1. Install the OBS plugin (restart OBS).
2. Add/choose your alert/TTS/browser audio source.
3. Open **Advanced Audio Properties**; put public audio on **Track 1** and
   private return audio on **Track 6**.
4. In the **IRL Audio Return** dock: paste your **pairing code**, confirm
   **Track 6**, click **Start** once.
5. In **Moblin**: add a **Browser widget** with the listener URL, **Audio only**
   mode.

Every stream after that: **nothing to do.** Launch OBS (plugin auto-streams),
open Moblin (widget auto-plays). Hear your private return audio.

## Common routing examples

| Goal | Track 1 (public) | Track 6 (return > phone) |
|------|------------------|--------------------------|
| Alerts only to streamer | mic, game | alerts |
| TTS only to streamer | mic, game | TTS |
| Discord/mod/director voice to streamer | mic, game | Discord |
| Alerts to both stream and streamer | mic, game, **alerts** | alerts |
| Clean VOD + private return | mic, game (VOD on Track 2) | alerts/TTS |

## Safety warnings the plugin shows

- **Track 1 selected** - warns that Track 1 usually carries your full public mix;
  it does not block you, in case that's intentional.
- **No audio for 10s+** - the selected track has been silent; check your routing.
- **Ring buffer overflow / dropped frames** - surfaced in Status if the worker or
  network can't keep up.
- **Auth failed / Relay unreachable** - shown in the State + Last error fields.

## Privacy

The stream key/token is never logged in plaintext and is masked in the UI after
saving. The **Export diagnostic bundle** button writes a support file with all
secrets redacted.
