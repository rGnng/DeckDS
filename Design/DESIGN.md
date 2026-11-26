# DECK DS — Complete Product, UI & Native Engineering Handoff Specification

> **For Implementing Agents & Engineers:**  
> This document is the single source of truth for building the **native `aarch64` Linux** version of **DECK DS**. The interactive prototype in [`Design/`](./) (`index.html`, `styles.css`, `app.js`, `audio-engine.js`, `vfd-icons.js`, `fonts/`) is the **verified visual, layout, icon, sound, and interaction reference** — **do NOT ship web technology (Electron, Chromium, WebKit) on the handheld.**

---

## 1. Target Hardware & Appliance Philosophy

- **Device:** ANBERNIC RG DS Plus
- **SoC / GPU / RAM:** Rockchip RK3568 (Quad Cortex-A55 @ 2.0 GHz), ARM Mali-G52 2EE, **1 GB RAM**
- **Displays:** Dual 4.3" IPS OCA Full-Lamination — **`1024 × 768 px` (4:3)** per screen (`87.4 × 65.5 mm`, 297.7 PPI, `1 mm ≈ 11.72 px`)
  - **Top Screen:** Passive non-touch display (Now Playing / VU / Spectrum stage)
  - **Bottom Screen:** Capacitive multi-touch + stylus + physical controls (100% Touch + 100% Keypad parity)
- **Controls:** D-Pad, ABXY, `L1 / R1 / L2 / R2`, `START / SELECT`, single left Analog Stick, 2 bottom-left Aux buttons (`STAR`, `NOW`), `KEY_POWER`, Hall magnetic clamshell switch (`SW_LID`)
- **Audio & Storage:** 3.5 mm headphone jack (bit-perfect ALSA direct), built-in stereo speakers, Bluetooth 4.2, dual MicroSD (up to 2 TB), Wi-Fi, 4000 mAh battery
- **Appliance Behavior:** Power on boots **straight into DECK DS** (~2–4 s) and **never exits** to an OS shell or launcher.

---

## 2. Recommended Native Linux Stack

| Layer | Recommended Native Implementation |
| :--- | :--- |
| **Language** | **Rust** (preferred for memory-safe real-time audio & easy `aarch64` cross-build via `cargo-zigbuild`) or **C++20** |
| **Window / GPU / GUI** | **SDL3 (or SDL2) + OpenGL ES 3.0** with a lean custom 2D UI/batch renderer + GLSL shaders (no GTK/Qt/WebKit bloat; ~25–45 MB total RAM) |
| **Typography & CJK** | **FreeType + HarfBuzz** (crisp unhinted integer bitmap rasterization for VFD mode; anti-aliased glyphs for Obsidian/Ceramic) |
| **Icons** | Build-time texture atlas generated directly from [`index.html`](./index.html) SVG symbols (Obsidian/Ceramic) and [`vfd-icons.js`](./vfd-icons.js) 1-bit pixel bitmaps (VFD) |
| **Audio Engine** | Lock-free real-time decode thread (**`symphonia`** or **FFmpeg `libav`** for FLAC/ALAC/WAV/MP3/AAC/OGG/DSD) → unity-gain pre-fader ring buffer → direct **ALSA** (`hw:0,0`) / **BlueZ** output |
| **Library & Persistence** | **SQLite** (WAL mode on `/data/deckds.db`) + `config.toml` + disk thumbnail cache (`288×288` & `80×80` QOI/raw) + lazy background 80-bar waveform peak cache |
| **OS Strategy** | **Phase 1:** ROCKNIX / ROCKNIXDS in kiosk mode (replace EmulationStation with `deckds.service`, `Restart=always`, read-only squashfs root). **Phase 2:** Minimal ~40 MB read-only Buildroot appliance image reusing the verified RK3568 kernel + `.dtb` + Mali blob. |
| **Platform HAL** | Clean `PlatformHAL` trait/interface with `hal_macos` (dual `1024×768` SDL windows + CoreAudio for desktop development) and `hal_rgds` (`KMS/DRM` dual-display, `libevdev` gamepad/touch/`SW_LID`/`KEY_POWER`, `sysfs` backlight & battery, ALSA mixer, BlueZ D-Bus, ConfigFS USB gadget). |

---

## 3. Visual Design System & Strict Rules

### 3.1 Strict Invariants (Do Not Violate)
1. **Strict Even-Integer (`2 px`) Geometry:** Every width, height, margin, padding, gap, and border inside both `1024 × 768` screens MUST be an even integer (zero `.5 px` or odd-pixel dimensions).
2. **Edge Alignment with `L1` / `R1`:** Outer left edge of all bottom-screen controls, bars, lists, grids, and the on-screen keypad aligns flush with `L1` (`x = 16 px`); outer right edge aligns flush with `R1` (`x = 1008 px`, exact **`992 px`** span). The `64 px` right-hand scrollbar aligns on **both** left (`x = 944 px`) and right (`x = 1008 px`) edges with `R1`.
3. **Zero `:hover` Effects:** Real target hardware is a touchscreen with no hover. Use only crisp physical press depression (`.is-pressed`) and keypad focus (`.kp-focus`). Do **not** add expanding touch ripple/wave animations.
4. **Zero Middle Dots (U+00B7):** Never use middle-dot separators anywhere in UI strings, metadata, or docs (use spaces, commas, slashes, or em-dashes instead).
5. **Restrained Accent Discipline (Anti-Glare):**
   - **Selected Tab:** Elevated neutral card + accented icon (no solid neon fill, no bottom underline).
   - **Hero Play/Pause Key:** Matte elevated keycap (`var(--bg-card-hover)`) + accented icon + `56 × 4 px` recessed bottom accent rail (no solid orange/cyan flood fill, no glow halo).
   - **Star Button (`DECK`):** Neutral card; starred state shown solely via filled star `#ic-star-filled` (no accent border/fill).
   - **`-1` Utility Bar:** Neutral buttons; active Grid/List segment uses calm neutral gray (`#323745` Obsidian, `#d4cdc0` Ceramic, `#133540` VFD) so only the keypad focus ring stands out.
   - **Focus Cursor on Rows/Cards:** Uniform 4-sided `border-color: var(--accent)` + `inset 0 0 0 2px var(--accent)` + soft outer ring (NO thick left bar).
   - **Status Bar, Top Artist, Starred Playlist, Shuffle/Repeat, Get Info, Format Badge:** All neutral monochrome tones (no green battery, no battery `%`, no orange artist highlight, no permanently highlighted Starred playlist).

### 3.2 Three Visual Themes (Exact Color Tokens)

| Token | **Obsidian** (Default) | **Ceramic** | **VFD** (Phosphor Dot-Matrix) |
| :--- | :--- | :--- | :--- |
| `--bg` | `#0c0d10` | `#eae5dc` | `#03080a` |
| `--bg-elev` | `#15171d` | `#dfd9cd` | `#071115` |
| `--bg-card` | `#1c1f27` | `#f7f4ed` | `#09171c` |
| `--bg-card-hover` | `#252934` | `#ffffff` | `#0e242b` |
| `--border` | `#2c313d` | `#ccc4b5` | `#133540` |
| `--border-strong` | `#414859` | `#aba291` | `#1f5563` |
| `--text` | `#f5f2ea` | `#16171b` | `#72ffe0` |
| `--text-dim` | `#aeb4c2` | `#494c55` | `#36b8a2` |
| `--text-muted` | `#767d8e` | `#73767f` | `#1e6b62` |
| `--accent` | `#ff7a1a` | `#e6481e` | `#38f2c6` |
| `--accent-soft` | `rgba(255,122,26,0.18)` | `rgba(230,72,30,0.14)` | `rgba(56,242,198,0.16)` |
| `--accent-ink` | `#120b05` | `#ffffff` | `#03120f` |
| `--danger` | `#ff4757` | `#d92d3a` | `#ff4a5e` |
| `--vu-bg` / `--vu-glow` | `#171410` / `#382a1a` | `#f4eedf` / `#fff9ea` | `#040e12` / `#38f2c6` |
| `--wf-inactive` | `#343948` | `#c3bbb0` | `#0f2c33` |

### 3.3 Strict 5-Step Hardware Type Scale

Zero arbitrary font sizes inside either `1024 × 768` screen.

| Token | Obsidian / Ceramic | **VFD (Exact `2 px` Dot-Pitch Multiples)** | Physical (4.3") | Role |
| :--- | :--- | :--- | :--- | :--- |
| `--t-display` | **`48 px`** (`Space Grotesk`) | **`48 px`** (`4×` `PixelMplus12`) | `4.1 mm` | Hero cover title, scrubber popup hero |
| `--t-title` | **`32 px`** (`Space Grotesk`) | **`32 px`** (`2×` `DotGothic16`) | `2.7 mm` | Top artist, unified dock title, detail/sheet headers |
| `--t-body` | **`26 px`** (`Space Grotesk` / `IBM Plex Mono`) | **`24 px`** (`2×` `PixelMplus12`) | `2.2 mm` | List item names, timecodes, volume popup, OSK keys, sheet actions |
| `--t-sub` | **`21 px`** (`Space Grotesk` / `IBM Plex Mono`) | **`20 px`** (`2×` `PixelMplus10`) | `1.8 mm` | Subtitles, status bar, active tab, buttons, search input |
| `--t-cap` | **`18 px`** (`IBM Plex Mono`) | **`16 px`** (`1×` `DotGothic16`) | `1.54 mm` | Micro captions, counts, notches, inline keycaps (`Y`, `SEL`) |

### 3.4 Authentic VFD Dot-Matrix Rendering Rules
- **Unified `2.00 px` VFD dot pitch** across fonts, pixel icons (`8×8` @ `16 px`, `16×16` @ `32 px`, `24×24` @ `48 px`, `32×32` @ `64 px` from [`vfd-icons.js`](./vfd-icons.js)), `4 × 4 px` unlit dot-grid texture, `2 px` squared corners, segmented progress/battery/slider tracks, and a global `1 px / 2 px` anode grid mask shader.
- **Album covers are strictly exempt** from pixelation and the VFD anode mask so artwork stays razor-sharp full resolution.

---

## 4. Hardware Input Map (100% Keypad + 100% Touch Parity)

| Control | Behavior |
| :--- | :--- |
| **`L1 / R1`** | Bounded top-tab navigation (**Left stop = `DECK`**, **Right stop = `QUEUE`**, no wrap-around; boundary `bump` SFX at edges). |
| **`D-Pad ↑ ↓ ← →`** | **On `DECK` (no visual cursor):** `↑ / ↓` Volume `±5` (shows floating popup); `← / →` short tap (`< 320 ms`) = Prev / Next track, **long press (`≥ 320 ms`)** = continuous silent seek (no seek SFX). **On 1-column lists** (Albums List, Artists, Playlists, Songs, Queue, Detail tracks): `↑ / ↓` step item, **`← / →` Page Up / Page Down** (smooth cubic-out scroll). **On Grids / Settings / Sheets / On-Screen Keypad:** 2D spatial cursor. |
| **`A`** | Activate focused control / Play track / Open Album, Artist, or Playlist / On `DECK`: Play/Pause (or **Shuffle All** when stopped). |
| **`B`** | Close OSK / Clear Search / Tuck `-1` bar / Step back from Detail / Exit Settings to `DECK` / Close active sheet. |
| **`Y`** | Open Context Menu (`⋯`) for focused track (or current track on `DECK`). Inside On-Screen Keypad: quick **Backspace**. |
| **`X`** | Cycle Top Stage (`COVER → VU → SPECTRUM`). |
| **`SELECT`** | Open **Order By** sheet (Albums / Artists / Songs), **New Playlist** (Playlists), or **Settings** (on `DECK`). |
| **`L2 / R2`** | Global Previous / Next Track. |
| **`START`** | Global Play / Pause (or **Shuffle All** when stopped & Up Next is empty). |
| **Left Aux 1 (`STAR`)** | Global Star / Unstar current track. |
| **Left Aux 2 (`NOW`)** | Smart Jump (`DECK` ↔ Current Track's Album Detail). |
| **Analog Stick** | Scrub playback position on `DECK`. |
| **Hall Switch (`SW_LID`)** | **Pocket Audio:** cut both backlights to `0`, halt 60 fps UI loop, set CPU governor to `powersave`, keep ALSA playing + `L2/R2/START` active. **Pause:** pause & suspend. |
| **`KEY_POWER`** | Short press: screen off / pocket audio (or sleep if stopped). Long press (~3 s): flush SQLite & graceful shutdown. |

---

## 5. Top Screen Specification (`1024 × 768` — Passive Non-Touch)

### 5.1 Top Status Bar (`64 px` tall)
- Unified neutral `--font-mono` / `--t-sub` / `color: var(--text-dim)` on `var(--bg-elev)` with `2 px` bottom divider.
- **Left:** `24 × 24 px` (`32 × 32 px` VFD) transport state icon (`#ic-play` playing, `#ic-pause` paused, **`#ic-stop` solid square when stopped**, icon-only — zero text) | Queue position (`01 / 12`, or `00 / 00` when stopped) | Codec/sample rate (`FLAC 96kHz`, empty when stopped).
- **Right:** `24 × 24 px` (`32 × 32 px` VFD) filled star (`#ic-star-filled`, visible only when current track is starred) | Monochrome battery icon (no green, no `%`) | Clock (`21:26`).

### 5.2 Three Visual Stages (`1024 × 704 px` Viewport + Unified Frameless Bottom Dock)
A single pixel-locked frameless `.stage-now-dock` sits at `left: 24px; right: 24px; bottom: 24px` across all 3 views (`#stageNowTitle` shown on `VU` and `SPECTRUM`, hidden on `COVER` to avoid duplicating the hero title; progress bar + elapsed/total timecodes identical on all 3 views). All Top Screen text lines (`cover-title`, `cover-artist`, `cover-album`, `stage-now-title`) are strictly **single-line** (no line breaks, no ellipsis) with a dynamic **`48 px` right-edge fade mask** applied only when overflowing (`scrollWidth > clientWidth`).

1. **`COVER`:**
   - Unobstructed **`544 × 544 px`** album art (zero badges, zero gloss overlay) + vertically centered single-line Title (`--t-display`, `--text`), Artist (`--t-title`, `--text-dim`), Album (`--t-body`, `--text-muted`) above the shared bottom progress bar. No year, no Up Next preview.
2. **`VU`:**
   - Dual **`478 × 540 px`** meters (`20 px` center gap = `976 px` total, clipped interior frames, `-0.70π..-0.30π` sweep, `r = 336` analog / `324` VFD so ticks/labels/needles stay strictly inside `[34.5..443.5]` with zero edge bleed).
   - **Obsidian:** Dimmed warm tungsten backlight (`#382a1a` → `#171410` → `#0e0c0a`), major/minor ticks (`-20, -10, -7, -3, 0, +3`), needle shadow, peak LED.
   - **Ceramic:** Warm ivory dial (`#fffdf7` → `#f4eedf` → `#ddd4c2`) with charcoal markings.
   - **VFD:** 24-segment radial phosphor arc + decaying peak-hold segment + `PEAK` block on `8 px` dot-matrix grid.
3. **`SPECTRUM`:**
   - `1024 × 704 px` stereo oscilloscope (`y = 112`) + 28-band FFT (`baseLine = h - 138`, `maxH = 344`) above the shared song title and progress bar.
   - **Obsidian:** Off-white oscilloscope + slate-to-steel-to-amber FFT bars (`#222630 → #414859 → #8a92a4 → #ff7a1a`) + peak caps.
   - **Ceramic:** Charcoal oscilloscope + warm stone-to-charcoal-to-vermilion FFT bars (`#c8c0b2 → #8b857a → #3f424a → #e6481e`).
   - **VFD:** Quantized `8 px` stepped oscilloscope + 28×22 segmented phosphor columns + peak hold.

### 5.3 Stopped / Idle State (End of Up Next)
When the last track finishes (or `Next` is pressed with empty Up Next):
- **No text** anywhere (empty title, artist, album, codec, waveform title).
- **Empty cover placeholder** (subtle concentric disc outline on `var(--bg-card)`, no ambient glow) and flat empty waveform baseline.
- **Zero progress:** `00:00 / 00:00`, `0%` bar, `00 / 00` queue position, `#ic-stop` square in top status bar.
- Bottom hero transport button switches to **`#ic-shuffle` (Shuffle All)**.

---

## 6. Bottom Screen Specification (`1024 × 768` — Touch + Keypad)

### 6.1 Top Navigation Bar (`76 px` tall, `padding: 0 16px`)
```
[L1]  [ ▶ DECK ]  [💿]  [👤]  [♫]  [♪]  [≡]  [R1]
```
- `L1` / `R1`: `64 × 52 px`, dimmed at boundaries.
- Active tab expands (`flex: 3`, `300 px`) showing icon + label; inactive tabs stay compact (`flex: 1`, `100 px`) icon-only.

### 6.2 Tab 1: `DECK` (`992 px` inner width, `padding: 16px 16px 18px`, `gap: 16px`)
1. **Waveform Card (`flex: 1`):** Track title | Elapsed/Total timecodes | `⋯ [Y]` Context Menu | `⚙ [SEL]` Settings | Interactive 80-bar (or `8 px` VFD dot-matrix) touch/stylus scrubber canvas.
2. **Transport Row (`172 px` tall, `gap: 16px`):** Exact squares **`172 × 172`** (`Prev`), **`428 × 172`** (`Play/Pause` — becomes `Shuffle All` when stopped & Up Next empty), **`172 × 172`** (`Next`), **`172 × 172`** (`Star`, icon-only fill toggle). Prev/Next support short tap = skip, hold `≥ 320 ms` = silent seek.
3. **Single-Line Volume Bar (`94 px` tall):** Mute toggle button (`58×58`) | Horizontal slider with floating `#volPopup` bubble above thumb **only while adjusting** (no static volume number in bar) | Output Device icon-only button (`58×58`, opens Audio Output sheet).
4. **4 Quick Pads (`104 px` tall, `gap: 16px` → `236 × 104 px` each):** Stage cycle icon (`lucide:disc-album` / `carbon:meter` / `foundation:graph-bar`, no `X` highlight), Shuffle (`bi:shuffle`, subtle bottom dot when active), Repeat (`bi:repeat` / `bi:repeat-1`, subtle dot when active), Go to Current Album (`#ic-goto-album`).

### 6.3 Tabs 2–5: `ALBUMS`, `ARTISTS`, `PLAYLISTS`, `SONGS`
- **`-1` Pull / Press-Up Utility Bar:**
  - Hidden by default (full `692 px` viewport height for content). Revealed by pressing `UP` from the top row (**default keypad focus lands on `Play`**) or pulling/wheeling up at top of list.
  - **Left:** Inline Search Box (`320 px` unified fixed width across all 4 tabs when idle; expands to **`100%` full bar width** hiding other buttons when active, showing live `#catHeaderCount` + `✕` clear button).
  - **Right:** Icon-only `Shuffle` & `Play` (labels expand smoothly on `.kp-focus`), `Grid / List` view toggle (`ALBUMS` only, persisted), `Order By` button (`Added Time ↓`, opens unified Order By + Direction sheet) or `＋ New Playlist` (`PLAYLISTS`).
- **List & Grid Layouts (All flush with `L1` / `R1`, all with `64 px` right-hand scrollbar):**
  - **Albums Grid:** 3 columns (`296 × 368 px` tiles, `14 px` gap, no release year, bare numeric track count badge). **Albums List:** 1-column full-width rows (`118 px` tall).
  - **Artists:** 1-column rows (no profile avatar, single subtitle `N Albums, Y Songs`). **Artist Detail:** 2-column Albums grid (`450 px` wide cards) seamlessly navigable by 2D D-Pad into Detail Header (`-3..-1`) and Songs list.
  - **Playlists:** 1-column rows (`Starred` + custom playlists, bare numeric count, zero `"Built-in / Custom"` filler).
  - **Songs:** Virtualized 1-column rows (`116 px` tall); now-playing `#ic-play` badge overlaid on bottom-right of album thumbnail (never on song title).
  - **Album Detail:** Multi-disc dividers (`DISC 1`, `DISC 2`), single-line track rows (`92 px` tall), `#ic-play` inside `.track-no-badge.is-playing`.
- **Universal `64 px` Right-Hand Scrollbar/Scrubber:** Present on **every** list view (Albums, Artists, Playlists, Songs, all Detail views, and Queue). Fixed `44 px` GPU-translated thumb (`translate3d`), direct finger-locked drag, contextual ticks (`A–Z` / `Year` / `#`) + floating jump popup.
- **On-Screen Touch + D-Pad Virtual Keypad (`#dsOsk`):**
  - Anchored at bottom (`left: 16px; right: 16px; bottom: 16px` = `992 px` wide), 10-column grid of exact **`90 × 58 px`** keys (4 rows: `ABC` QWERTY + `123` symbols, `⌫`, `SPACE`, `CLR`, `DONE`), 100% operable by touch and 2D D-Pad (`A` types, `Y` backspace, `B`/`DONE` dismisses into live filtered list). Used for both Search and New Playlist creation.

### 6.4 Tab 6: `QUEUE` (True iTunes / Apple Music Up Next)
- `state.queue` contains **only upcoming tracks** (never the currently playing track).
- Selecting a track in a list puts only subsequent tracks into `state.queue` and preceding tracks into `state.history`.
- Playing any song removes it from Up Next; `Prev` restores from `state.history` (pushing the current track back to top of Up Next); `Play Next` inserts/moves to index `0`; default `repeat = 'off'`; finishing the last song enters the **Stopped / Idle** state.
- Every row has a dedicated `Play Next` button (bar above upward arrow) + `⋯` Context Menu button, plus the unified `64 px` right-hand scrollbar.

### 6.5 Universal Sheets & Settings
- **Track Context Menu (`Y` / `⋯`):** 2-column grid with **left-aligned icon rail** (`32 × 32 px` fixed icon slot + left-aligned label + right-aligned neutral toggle values `On`/`Off`/`All`/`One` for Shuffle & Repeat on `DECK`). Tailored per context (no `Add to Queue` inside Queue; includes `Remove from Queue` / `Remove from Playlist` and `Get Info`).
- **Get Info Sheet:** Scrollable, keypad-operable audiophile spec grid (Album, Artist, Year, Disc/Track, Duration, Codec, Sample Rate & Bit Depth, Bitrate, Channels, File Size, Added Date, ReplayGain, Signal Path, MicroSD File Path).
- **Order By Sheet:** Unified `ORDER BY` fields (`Added Time` first) + context-clear `DIRECTION` (`↓ Newest First` / `↑ Oldest First`, `↑ A → Z` / `↓ Z → A`, etc.).
- **Settings (`⚙` on `DECK`):** 7-Notch Screen Brightness (`15% → 100%` with detent chimes), Visual Theme (`Obsidian` / `Ceramic` / `VFD`), Lid Close Hall Sensor (`Pocket Audio` / `Pause`), UI Sound FX (`Nintendo SFX` / `Mute`), plus native appliance items (USB Transfer Mode, BT Pairing, Rescan Library, LAN Debug toggle, Power Off).

---

## 7. Audio Engine & Procedural Nintendo Two-Tone SFX

- **Pre-Fader Metering:** Audio graph routes `decode/source → sourceBus (unity 1.0) → [Analyser FFT + Stereo L/R RMS]` AND `sourceBus → masterGain (volume/mute) → hardware output`. VU meters and Spectrum always reflect the audio file itself, independent of volume or mute.
- **Procedural UI SFX (Reference Frequencies in [`audio-engine.js`](./audio-engine.js)):**
  - Two-tone chimes split across press-down (`:down`) and release (`:up`) on interactive buttons:
    - `play`: ascending 2-tone fifth (`G5 784 Hz → D6 1174.7 Hz`)
    - `pause`: descending 2-tone fifth (`D6 1174.7 Hz → G5 784 Hz`)
    - `confirm`: `C6 1046.5 Hz → G6 1568 Hz`
    - `back`: `G5 784 Hz → D5 587.3 Hz`
    - `deck` (`L1/R1`/tabs): `420→260 Hz` snap + `1560→1840 Hz` click
    - `stage` (`X`): `1174.7→1568 Hz` + `1760 Hz`
    - `add`: `F6 1396.9 Hz → A6 1760 Hz`
    - `star`: rising arpeggio `C6 → E6 → G6 → C7`; `unstar`: `E5 → C5`
    - `press` / `release`: crisp mechanical dome switch tock/tick
    - `bump`: soft `210→140 Hz` boundary thud
  - **Zero seek sound FX** (holding `←/→` or scrubbing waveform is completely silent).

---

## 8. Native Appliance Services: CJK Fuzzy Search, Opt-In OTA & LAN Debug / Web Control

- **CJK Fuzzy Search on On-Screen Keypad (Kanji / Hanzi / Kana):**
  - Typing ASCII romaji / pinyin / initials on the 4-row QWERTY keypad matches **Kanji, Hanzi, Hiragana, Katakana, and Latin** titles & artists without needing an on-device IME.
  - Background SQLite indexer precomputes normalized search columns: Japanese readings (Kanji → Kana & Romaji, e.g. `深夜` → `しんや` / `shinya`), Chinese Pinyin + first-letter initials, Hiragana ↔ Katakana folding, and Latin/diacritic folding (`ō` → `o`), with fuzzy subsequence matching.
- **Opt-In OTA Updates (Empty by Default, Configured via LAN Web Config):**
  - **OTA URL is empty by default** — zero phoning home and zero forced repo checks.
  - Users configure their preferred update endpoint (e.g., GitHub Releases API for `rGnng/DeckDS`, a personal fork, or a self-hosted LAN URL) strictly via **LAN Web Config** (or `/data/config.toml` — no URL typing on the handheld).
  - Supports SHA-256 verification, atomic A/B staging swap with rollback safety, and automatic `deckds` restart.
- **LAN Debug, Config & Remote Web Control (Built-In Native HTTP/WS Server):**
  - Lightweight native HTTP/WebSocket server exposed on a LAN port (e.g. `http://<device-ip>:8080`, toggleable in Settings) for external browsers on phone/PC (zero browser runs on the handheld).
  - Provides live logs & telemetry (ALSA sample rate, DRM/evdev monitor, CPU/RAM/battery), OTA URL & binary upload, library rescan, Wi-Fi/BT/playlist editing, and full LAN remote playback control.
