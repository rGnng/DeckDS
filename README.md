# <img src="Design/deckds-logo.svg" width="280" alt="DeckDS">

**DeckDS** is a native dual-screen music player for the **ANBERNIC RG DS Plus** (dual 4.3" `1024 × 768` IPS displays, RK3568, 64-bit Linux).

- [`Design/`](Design/) contains the interactive browser reference and the [native handoff](Design/DESIGN.md).
- [`Native/`](Native/) contains the SDL3 implementation and desktop preview.

## SDL3 preview

On macOS, install the build dependencies and run these commands from the repository root.

```sh
brew install cmake python node sdl3 sdl3_ttf sdl3_image
cmake -S Native -B Native/build -DCMAKE_PREFIX_PATH="$(brew --prefix)" -DCMAKE_BUILD_TYPE=Release
cmake --build Native/build --parallel
open Native/build/DeckDS.app
```

On Linux, install the SDL3, SDL3_ttf and SDL3_image development packages with SDL_image SVG support, plus Python 3, Node.js and CMake. Node.js runs the web demo's fixture generator at build time. Configure and build with the same commands, omitting the Homebrew prefix if the libraries are installed system-wide. Launch `Native/build/DeckDS`.

The Audio Output sheet models the RG DS Plus headphone DAC, speakers, and Bluetooth output. Bluetooth pairing uses demo devices on macOS. On Linux, install the GIO development package (`gio-2.0` in pkg-config) at build time and run with BlueZ and an enabled adapter to discover and pair audio devices. Playback in the preview uses the host’s default audio output.

Run with `--bluetooth-demo` to open Audio Output with ten simulated paired devices. On macOS, use `open -n Native/build/DeckDS.app --args --bluetooth-demo`. Changes to the simulated devices are discarded on exit.

The main window scales two fixed 1024 × 768 panels. The right panel provides clickable RG DS Plus controls, theme switching, and five display modes. **Fit** fills the available space. **Integer** snaps to a whole-number pixel scale with a minimum of 1×. **1× Pixels** and **2× Pixels** use 1024 × 768 and 2048 × 1536 backing pixels per panel. **Studio Display 5K / 1:1** calibrates for the 27-inch, 218 PPI Studio Display and targets 87.4 × 65.5 mm per panel. Use the display's native Retina mode for physical pixel matching. Right-drag or Option-scroll to pan an oversized preview.

Click the lower panel to simulate touch. Use **Keys** for keyboard shortcuts or **F2** to cycle display modes. Run with `--panels` to preview the two panels in separate windows. On macOS, use `open Native/build/DeckDS.app --args --panels`.

Space starts the synthesized demo track. **C** (the RG DS Plus X button) cycles Cover, VU and Spectrum. **Q/E** change tabs, **Up** at the top of a library view reveals its toolbar, and **/** opens search and the touch keypad. The toolbar's library control switches between the web demo's 100- and 2,000-album catalogs. The SDL3 build has been exercised on macOS and desktop Linux. The two-panel kiosk mode and the handheld's display, input and appliance services still need RG DS Plus testing.

In a top-level list or grid, tap **B** to reset the viewport and cursor to the start. In Queue, it resets to the first Up Next song. Hold **B** for half a second and release to return to Deck.

On the Deck tab, **Tab** opens Settings. **Time Format** switches the computer’s local time between 12-hour and 24-hour display on the top screen.

The SDL3 preview remembers display and audio settings, library layout and sorting, and the last tab across relaunches. It restores favorites, playlists, the queue, and the last track and position. Playback opens paused.

Queue shows Played and Up Next with the current song between them. Opening Queue starts at Up Next and focuses its first song. Selecting a song plays it immediately and keeps the remaining Up Next order. After the queue finishes, Play starts the played sequence again.

Run with `--long-metadata` to preview a track with long English and Japanese names in a separate preview session. On macOS, use `open Native/build/DeckDS.app --args --long-metadata`. Add `--dump-views <dir>` to capture the long-name test views as PNGs.

### Local music library

Install FFmpeg and SQLite development libraries and pkg-config, then configure and rebuild. On macOS, use `brew install ffmpeg pkgconf` and the SQLite library from the macOS SDK. On Debian or Ubuntu, install `pkg-config libavformat-dev libavcodec-dev libavutil-dev libswresample-dev libsqlite3-dev libicu-dev`.

```sh
open -n Native/build/DeckDS.app --args --music-path "/path/to/Music"
# Linux
Native/build/DeckDS --music-path "/path/to/Music"
```

Use **Music Folder** in the preview’s left column to add a music folder. DeckDS opens the combined catalog from `library.sqlite` in its local preferences, then checks the configured folders in the background. Source files are read-only. The combined library keeps its saved playback position, queue, history, favorites, and playlists across restarts.

Under **Deck Settings → Music Library**, **Quick Scan** and **Full Scan** read tags for new files and reuse indexed metadata when both file size and modification time match. The preview’s **Rescan** button runs a quick scan. If a scan finds missing files, **Keep** leaves them in the index and marks them in Get Info. **Remove** drops the missing entries from the index.
The **Sync** control is reserved for a later update.

DeckDS reads embedded album art and common folder covers on demand. The Deck waveform uses fixed-scale RMS and loads the previous track and first Up Next waveform in the background. Embedded covers and waveforms are cached alongside the library index.

### Web Config

Install `libmicrohttpd` and `openssl@3` with Homebrew on macOS or `libmicrohttpd-dev` and `libssl-dev` on Debian or Ubuntu, then reconfigure and rebuild. On macOS, include `-DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)"` in the CMake configure command.

Web Config starts with DeckDS and listens on all IPv4 interfaces at port 8080. Open `http://127.0.0.1:8080` on the same computer. From another device, use the IPv4 address of the computer running DeckDS and port 8080. The Web Config password defaults to empty. A password enables the sign-in dialog. The browser remembers sign-in using a persistent cookie. Changing or clearing the password invalidates earlier cookies. DeckDS serves Bootstrap and all other browser assets locally.

The browser shows the active library, scan progress, and Now Playing. The Now Playing controls operate the DeckDS player, including previous and next track, play and pause, seeking, volume, and mute. Scans and missing-file decisions share the same state with the SDL3 app. Theme, accent color, brightness, clock format, SFX, SFX volume, SFX output, and lid behavior apply live and use the preview’s existing settings file.

Set each library’s **Sync URL** when adding or editing the library. Saving an empty field in **Edit** clears that library’s URL.

**Library** lists music folders and their current ID namespaces. Use **Add** to add a folder and **Edit** to update its settings. Fill in **Relocate to** in **Edit** to preview matches by full relative path and review unmatched files before moving. Confirm **ReKey** to back up the index and playback settings, recalculate Tracks-compatible song, album, and artist IDs, and update namespaces from the current identifiers.

Use **Disable** in **Edit** to take a library out of browsing, scanning, and playback. **Delete** is available for disabled libraries. After confirmation, DeckDS backs up the index and playback settings, then removes the library’s indexed tracks and their playlist references. Audio files stay on disk.

See [Library namespaces, UUIDs, and ReKey](docs/library-identity.md) for the ID formulas, database writes, and upgrade behavior.

**Connectivity** scans for visible open and WPA Personal Wi-Fi networks and joins them through NetworkManager on Linux or CoreWLAN on macOS. **Manual** joins a network by SSID and password. Linux requires GIO at build time and a NetworkManager-managed adapter at runtime. **Bluetooth Audio** discovers, pairs, connects, and forgets devices through the same BlueZ controller as the Deck on Linux. The macOS preview uses demo Bluetooth devices.

Under **Deck Settings → Updates**, use **Edit URL** to save or clear the device’s OTA URL. **Update** is disabled pending hardware support.

**Debug** opens the live diagnostics page at `/debug`. `GET /api/debug` returns the versioned JSON snapshot and the latest 96 in-memory DeckDS log entries. Use it to inspect the SDL display and audio drivers, active audio rates, controller and touch devices, power, memory, and scan status.
