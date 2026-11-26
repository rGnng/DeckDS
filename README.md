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

The main window scales two fixed 1024 × 768 panels. The right panel provides clickable RG DS Plus controls, theme switching, and five display modes. **Fit** fills the available space. **Integer** snaps to a whole-number pixel scale with a minimum of 1×. **1× Pixels** and **2× Pixels** use 1024 × 768 and 2048 × 1536 backing pixels per panel. **Studio Display 5K / 1:1** calibrates for the 27-inch, 218 PPI Studio Display and targets 87.4 × 65.5 mm per panel. Use the display's native Retina mode for physical pixel matching. Right-drag or Option-scroll to pan an oversized preview.

Click the lower panel to simulate touch. Use **Keys** for keyboard shortcuts or **F2** to cycle display modes. Run with `--panels` to preview the two panels in separate windows. On macOS, use `open Native/build/DeckDS.app --args --panels`.

Space starts the synthesized demo track. **C** (the RG DS Plus X button) cycles Cover, VU and Spectrum. **Q/E** change tabs, **Up** at the top of a library view reveals its toolbar, and **/** opens search and the touch keypad. The toolbar's library control switches between the web demo's 100- and 2,000-album catalogs. The SDL3 build has been exercised on macOS and desktop Linux. The two-panel kiosk mode and the handheld's display, input and appliance services still need RG DS Plus testing.

On the Deck tab, **Tab** opens Settings. **Time Format** switches the top screen between 12-hour and 24-hour time. **Set Clock** adjusts the time DeckDS displays by hours or minutes, and **Sync** returns it to the computer's local time. DeckDS saves the format and adjustment across restarts.
