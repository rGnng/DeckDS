# DeckDS

**DeckDS** is a dedicated native dual-screen Hi-Fi music player designed for the **ANBERNIC RG DS Plus** (dual 4.3" `1024 × 768` IPS displays, RK3568, 64-bit Linux).

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

Run with `--bluetooth-demo` to open Audio Output with ten simulated paired devices. On macOS, use `open -n Native/build/DeckDS.app --args --bluetooth-demo`. Changes made in this demo are discarded on exit.

The main window scales two fixed 1024 × 768 panels. The right panel provides clickable RG DS Plus controls, theme switching, and five display modes. **Fit** fills the available space. **Integer** snaps to a whole-number pixel scale with a minimum of 1×. **1× Pixels** and **2× Pixels** use 1024 × 768 and 2048 × 1536 backing pixels per panel. **Studio Display 5K / 1:1** calibrates for the 27-inch, 218 PPI Studio Display and targets 87.4 × 65.5 mm per panel. Use the display's native Retina mode for physical pixel matching. Right-drag or Option-scroll to pan an oversized preview.

Click the lower panel to simulate touch. Use **Keys** for keyboard shortcuts or **F2** to cycle display modes. Run with `--panels` to preview the two panels in separate windows. On macOS, use `open Native/build/DeckDS.app --args --panels`.

Space starts the synthesized demo track. **C** (the RG DS Plus X button) cycles Cover, VU and Spectrum. **Q/E** change tabs, **Up** at the top of a library view reveals its toolbar, and **/** opens search and the touch keypad. The toolbar's library control switches between the web demo's 100- and 2,000-album catalogs. The SDL3 build has been exercised on macOS and desktop Linux. The two-panel kiosk mode and the handheld's display, input and appliance services still need RG DS Plus testing.

In a top-level list or grid, tap **B** to reset the viewport and cursor to the start. In Queue, it resets to the first Up Next song. Hold **B** for half a second and release to return to Deck.

On the Deck tab, **Tab** opens Settings. **Time Format** switches the computer’s local time between 12-hour and 24-hour display on the top screen.

The SDL3 preview remembers display and audio settings, library layout and sorting, and the last tab across relaunches. It restores favorites, playlists, the queue, and the last track and position. Playback opens paused.

Queue shows Played and Up Next with the current song between them. Opening Queue starts at Up Next and focuses its first song. Selecting a song plays it immediately and keeps the remaining Up Next order. After the queue finishes, Play starts the played sequence again.

Run with `--long-metadata` to preview a track with long English and Japanese names in a separate preview session. On macOS, use `open Native/build/DeckDS.app --args --long-metadata`. Add `--dump-views <dir>` to capture the long-name test views as PNGs.

### Local music library

Install FFmpeg and SQLite development libraries and pkg-config, then configure and rebuild. On macOS, use `brew install ffmpeg pkgconf` and the SQLite library from the macOS SDK. On Debian or Ubuntu, install `pkg-config libavformat-dev libavcodec-dev libavutil-dev libswresample-dev libsqlite3-dev`.

```sh
open -n Native/build/DeckDS.app --args --music-path "/path/to/Music"
# Linux
Native/build/DeckDS --music-path "/path/to/Music"
```

Use **Music Folder** in the preview’s left column to choose the single active library folder. DeckDS opens the last catalog from `library.sqlite` in its local preferences, then checks the folder in the background. It reads tags for new files and files whose size or modification time changed. Missing files stay in the index and are marked in Get Info. The source files are read-only, and each folder keeps its own saved playback position, queue, favorites, and playlists.

DeckDS reads embedded album art and common folder covers on demand. The Deck waveform uses fixed-scale RMS and loads the previous track and first Up Next waveform in the background. Embedded covers and waveforms are cached alongside the library index.
