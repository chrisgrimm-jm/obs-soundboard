# OBS Soundboard

A plugin for OBS Studio that adds a broadcast-style soundboard — one scene holding all your sound clips, nested on top of every other scene (same idea as a downstream keyer), with a dock that punches clips in on demand.

Built by [Jomboy Media](https://jomboymedia.com).

---

## What It Does

You create one OBS scene that holds every sound clip you might want to fire during a show. The plugin adds that scene as a layer on top of every other scene in your collection (audio-only sources render no pixels, so it never gets in the way visually), then gives you a dock panel with a play button per clip.

Every clip is independent, can be trimmed to a specific in/out point, can be routed to a monitoring device in addition to the main mix, and can be fired from the dock, a hotkey, or Bitfocus Companion.

---

## Installation

### Windows
1. Download `obs-soundboard-windows-x64.zip` from the [latest release](../../releases/latest)
2. Extract and copy `obs-soundboard.dll` to `C:\Program Files\obs-studio\obs-plugins\64bit\`
3. Restart OBS

### macOS
1. Download `obs-soundboard-macos-universal.tar.xz` from the [latest release](../../releases/latest)
2. Extract and copy the `.plugin` bundle to `~/Library/Application Support/obs-studio/plugins/`
3. Restart OBS

### Linux
1. Download the `.deb` from the [latest release](../../releases/latest)
2. Run `sudo dpkg -i obs-soundboard-*.deb`
3. Restart OBS

---

## Setup

1. **Create your Soundboard scene** — Make a new scene in OBS (e.g. `Soundboard`) and add each sound clip as a **Media Source** (Add Source → Media Source, one per clip).

2. **Open the dock** — Go to **Docks → Soundboard**.

3. **Configure the plugin** — Click the **Settings** button in the dock, select your Soundboard scene from the dropdown, and click **Add Soundboard scene to all scenes**. This nests it into your entire scene collection, on top of everything.

4. **Go live** — Click pads in the dock to fire clips during your show.

> If you add new scenes after initial setup, click **Add Soundboard scene to all scenes** again to include them.

---

## Features

### Dock Controls
Each clip in your Soundboard scene gets its own pad in the dock. Click to play, click again while it's playing to stop it early. Pads turn green while that clip is actively playing.

### Per-Clip Trim + Outputs
Right-click any pad to open its settings:
- **Start (in point)** / **Duration** — trims playback to a specific range of the file; duration `0` plays to the file's natural end.
- **Extra Outputs** — in addition to always playing through the main program mix, a clip can also be played directly out of any number of your system's audio devices at once (headphones, an external monitor, a second speaker) — check as many as you want per clip. This plays independently of OBS's own single Monitoring Device setting, so different clips can go to different combinations of devices.
- **Test** — plays the clip immediately with the pending settings applied.

### Hotkeys
Every clip in your Soundboard scene automatically gets a hotkey registered under **OBS Settings → Hotkeys** — look for entries starting with `Soundboard: Play`.

### Bitfocus Companion Integration
The plugin runs an HTTP server (default port `4489`, listening on every network interface, not
just localhost) for integration with [Bitfocus Companion](https://bitfocus.io/companion) — Companion
can run on a different machine on the same network (e.g. a dedicated control PC or Stream Deck box)
than the one running OBS; just point its config at the OBS machine's LAN IP address instead of
`127.0.0.1`. This API has no authentication, so only run it on a network you trust, same as
`obs-websocket` without a password set.

For the full Companion experience — a live dropdown of clip names, a feedback that colors a button
while its clip plays, and time-remaining variables — use the real Companion module at
[obs-soundboard-companion](https://github.com/chrisgrimm-jm/obs-soundboard-companion) rather than
hitting this API with Companion's generic HTTP action. Raw endpoints, if you want them directly:

| Method | Endpoint | Description |
|--------|----------|-------------|
| `GET` | `/api/status` | Returns the scene name and every clip's playing state + time remaining |
| `GET` | `/api/clips` | Same as `/api/status` |
| `GET` | `/api/clip/:name` | Just that one clip |
| `POST` | `/api/clip/:name/play` | Plays the clip |
| `POST` | `/api/clip/:name/stop` | Stops the clip |
| `POST` | `/api/stopall` | Stops every clip |

Source names in the URL must be URL-encoded. The port can be changed in Settings.

---

## Building From Source

Requires CMake 3.28+, a C++17 compiler, and an internet connection (the build system auto-downloads OBS and Qt dependencies).

**Windows**
```
cmake --preset windows-x64
cmake --build --preset windows-x64
```

**macOS** (requires full Xcode.app, not just Command Line Tools — the build's own CMake config enforces the Xcode generator)
```
cmake --preset macos
cmake --build --preset macos
```

**Linux**
```
cmake --preset ubuntu-x86_64
cmake --build --preset ubuntu-x86_64
```

CI (`.github/workflows/`) builds all three platforms on every push to `main` and uploads them as workflow artifacts.

---

## Requirements

- OBS Studio 28.0 or later
- Windows 10+, macOS 12+, or Ubuntu 22.04+
