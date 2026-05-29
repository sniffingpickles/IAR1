# IRL Audio Return

[![Download the latest release](https://img.shields.io/github/v/release/sniffingpickles/IAR1?label=Download&logo=github&color=2bb589)](https://github.com/sniffingpickles/IAR1/releases/latest)
[![macOS](https://img.shields.io/badge/macOS-universal-000000?logo=apple&logoColor=white)](https://github.com/sniffingpickles/IAR1/releases/latest)
[![Windows](https://img.shields.io/badge/Windows-x64-0078D6?logo=windows&logoColor=white)](https://github.com/sniffingpickles/IAR1/releases/latest)
[![License: GPL-2.0](https://img.shields.io/badge/License-GPL--2.0-blue.svg)](LICENSE)

Hear your alerts on your phone while you stream IRL. This is a free OBS plugin
that sends **one** audio track from OBS (your alerts, TTS, Discord, or mod
voice) to a private link you can open on your phone, in near real time.

It does not touch your main stream. No video, no extra audio cables, no
monitoring tricks. Provided for free by **IRLHosting**.

<br>

## What you need

1. A private link. Create one in a few seconds at
   **[alerts.irlhosting.com](https://alerts.irlhosting.com)** (no account, no
   sign-up). You get a **setup code** and a **listener link**.
2. OBS Studio 30 or newer.
3. This plugin, installed (see below).

<br>

## Install

Pick your system. Download the matching file from the
**[latest release](https://github.com/sniffingpickles/IAR1/releases/latest)**.

### <img src="https://img.shields.io/badge/-Apple-000000?logo=apple&logoColor=white" height="18"> macOS

1. Download `irl-audio-return-macos-universal.zip` and double-click to unzip.
   You will get a file named `irl-audio-return.plugin`.
2. In Finder, press **Cmd + Shift + G**, paste this folder path, and press Enter:
   ```
   ~/Library/Application Support/obs-studio/plugins/
   ```
   If the `plugins` folder does not exist, create it.
3. Drag `irl-audio-return.plugin` into that folder.
4. Quit and reopen OBS.

If OBS does not show the plugin after restarting, macOS may have quarantined the
download. Open the **Terminal** app and paste this one line, then restart OBS:
```
xattr -dr com.apple.quarantine ~/Library/Application\ Support/obs-studio/plugins/irl-audio-return.plugin
```

### <img src="https://img.shields.io/badge/-Windows-0078D6?logo=windows&logoColor=white" height="18"> Windows

1. Download `irl-audio-return-windows-x64.zip`.
2. **Important - unblock the file first.** Right-click the downloaded `.zip`,
   choose **Properties**, tick **Unblock** at the bottom, then click **OK**.
   Do this **before** extracting. If you skip it, Windows marks the files inside
   as blocked and the plugin's bundled libraries will fail to load.
3. Now unzip it (right-click, **Extract All**).
4. Inside you will find an `obs-plugins` folder and a `data` folder. Copy both
   into your OBS install folder, usually:
   ```
   C:\Program Files\obs-studio\
   ```
   When Windows asks, choose **Replace** or **Merge** the folders.
5. Quit and reopen OBS.

If you already extracted without unblocking and OBS does not list the plugin,
delete the copied files, then redo steps 2-4 (unblock the zip first).

<br>

## Set it up (about a minute)

1. In OBS you now have an **IRL Audio Return** dock. If you do not see it, open
   **Docks** in the menu bar and turn it on.
2. Click **Settings** on the dock and paste your **setup code** (the long code
   from the website) into the **Pairing code** box. Click **Save**.
3. Send the audio you want your phone to hear to **Track 6**:
   - In OBS, open **Audio Mixer**, click the gear/menu icon on the source
     (for example your alerts or Discord), and choose **Advanced Audio
     Properties**.
   - Tick **Track 6** for that source. Leave your normal stream audio on
     **Track 1**.
4. Back on the dock, click **Start**. The status should change to **Connected**.

That is it. You can also turn on **Start automatically with my stream** so it
follows your OBS Go Live / Stop buttons.

<br>

## Listen on your phone

Open the **listener link** from the website on your phone. It starts playing on
its own. If you use **Moblin**, the website also shows a QR code that points
Moblin's built-in browser straight at your listener page.

For a full walkthrough with screenshots and Moblin tips, see
[docs/SETUP_OBS.md](docs/SETUP_OBS.md).

<br>

## Updating

Download the newest release and repeat the install steps, replacing the old
plugin file (macOS) or folders (Windows). Your settings are kept.

<br>

## Privacy

No audio is stored or recorded anywhere. Your link is private; keep it to
yourself. Anyone with the setup code can broadcast to your link, so do not share
it publicly.

<br>

## Having trouble?

See [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md), or open an
[issue](https://github.com/sniffingpickles/IAR1/issues).

<br>

---

<details>
<summary><b>For developers: build from source</b></summary>

<br>

This repository root is the plugin. It uses the official OBS plugin-template
CMake presets, which auto-download obs-studio, obs-deps, and Qt6 on configure.
Two extra libraries are fetched and built from source with `FetchContent`:
**libopus** (audio codec, linked statically) and **IXWebSocket** (the secure
WebSocket client; TLS uses SecureTransport on macOS and OpenSSL elsewhere).

Requirements: CMake 3.28 or newer, a C++17 compiler, and Xcode (macOS) or
Visual Studio 2022 (Windows).

**macOS (universal arm64 + x86_64)**
```bash
cmake --preset macos
cmake --build --preset macos
```
Output: `build_macos/RelWithDebInfo/irl-audio-return.plugin`.

**Windows x64**
```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64
```

**Core unit tests (no OBS/Qt, only libopus)**
```bash
cmake -S test -B build_coretest
cmake --build build_coretest
ctest --test-dir build_coretest --output-on-failure
```
These cover IAR1 encode/decode (exact big-endian wire-format parity with the Go
relay), the lock-free ring buffer, settings validation/masking, and Opus
encode/decode.

**Project layout**
```
src/core/   OBS-independent, unit-tested (libopus only):
            iar_packet, ring_buffer, opus_encoder, settings
src/net/    ws_client (IXWebSocket)
src/obs/    engine (raw-audio callback, worker thread, state machine, stats)
src/ui/     iar_dock, iar_settings_dialog (Qt6)
src/plugin_main.cpp    OBS module entry
data/locale/en-US.ini  UI strings
test/                  standalone core unit tests (CTest)
protocol/IAR1.md       wire protocol spec
```

Plugin version comes from `buildspec.json` and is compiled in as
`IAR_PLUGIN_VERSION`. Protocol version is `IAR_PROTOCOL_VERSION`.

The GitHub Actions **Build** workflow produces the installable macOS and Windows
artifacts attached to each release.

</details>
