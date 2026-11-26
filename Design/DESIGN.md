# DECK DS — Dual-Screen Music Player Design Specification (v6)

**Target Hardware:** ANBERNIC RG DS Plus  
**Displays:** Dual 4.3" IPS OCA Full-Lamination — **1024 × 768 px (4:3)** per screen (**297.7 PPI**, `87.4 × 65.5 mm`)  
**Apple Studio Display 27" 5K Calibration:** `109 CSS PPI` → **`375 × 281 CSS px`** per screen (`scale = 0.3662`) = exact 1:1 physical 4.3" size  
**Audio & I/O:** 3.5mm Headphone Jack (Bit-Perfect ALSA Direct), Stereo Speakers, Bluetooth 4.2, Dual MicroSD (up to 2 TB), Hall Magnetic Clamshell Switch

---

## 1. Dual Input Parity (100% Touch + 100% Keypad Operable)

Just like first-party Nintendo DS applications, **every single action on the bottom 4.3" screen can be operated completely by physical keypad OR by touch/stylus**, with a crisp accent focus cursor:

| Hardware Control | Action |
| :--- | :--- |
| **`L1 / R1`** | Bounded top-tab navigation (**Left stop = `DECK`**, **Right stop = `QUEUE`** — no wrap-around; subtle boundary thud at edges). |
| **`D-Pad ↑ ↓ ← →`** | **On `DECK` (No Cursor):** `↑/↓` Volume `±5`, `←/→` tap = Prev/Next track, **long-press `←/→`** = continuous seek. **On 1-Column List Views** (Albums List, Artists, Playlists, Songs, Queue, Detail tracks): `↑/↓` step by item, **`←/→` Page Up / Page Down**. **On Grids / Settings / Sheets:** Spatial 2D cursor. |
| **`A`** | Confirm / Activate focused control / Play track / Open Album, Artist, or Playlist. |
| **`B`** | Step back from Album/Artist/Playlist Detail, exit Settings to `DECK`, or close active sheet. |
| **`Y`** | **Evoke Context Menu (`⋯`)** everywhere (for focused track in Songs / Queue / Detail, or currently playing track on `DECK`). |
| **`X`** | Cycle Top Non-Touch Stage (`Cover → Analog VU → Spectrum`). |
| **`SELECT`** | Open **Sort Order** on `Albums` / `Artists` / `Songs`, **Create Playlist** on `Playlists`, or **Settings** on `DECK`. |
| **`L2 / R2`** | Global Previous / Next Track. |
| **`START`** | Global Play / Pause. |
| **Left Aux 1 (`STAR`)** | Star / Unstar currently playing track globally from any screen. |
| **Left Aux 2 (`NOW`)** | Smart Jump (`DECK` ↔ Current Track's Album Detail). |

---

## 2. Unified 5-Step Hardware Type Scale (Art System)

Zero arbitrary font sizes anywhere on either `1024 × 768` (`87.4 × 65.5 mm`) screen. Every UI element strictly maps to one of **5 physical type tokens**:

| Token | Pixels (Obsidian/Ceramic) | **VFD Dot-Matrix (Integer Scale)** | Physical (4.3") | Strict Category Role |
| :--- | :--- | :--- | :--- | :--- |
| **`--t-display`** | **`48 px`** | **`48 px`** (`3×` `DotGothic16`) | `4.1 mm` | Hero display (Cover title, scrubber jump pop-up hero) |
| **`--t-title`** | **`32 px`** | **`32 px`** (`2×` `DotGothic16`) | `2.7 mm` | Primary headings (Cover artist, category header title, sheet title, detail header) |
| **`--t-body`** | **`26 px`** | **`24 px`** (`2×` `PixelMplus12`) | `2.2 mm` | List items & primary values (Track/Album/Artist/Playlist names, timecodes, volume value, sheet actions) |
| **`--t-sub`** | **`21 px`** | **`20 px`** (`2×` `PixelMplus10`) | `1.8 mm` | Secondary metadata & interactive labels (Artist/album subtitles, active tab label, buttons, status bar) |
| **`--t-cap`** | **`18 px`** | **`16 px`** (`1×` `DotGothic16`) | `1.54 mm` | Micro captions, counts, uppercase labels, notch scale, inline hardware keycaps (`A`, `X`, `Y`, `SEL`) |

---

## 3. Top Screen (`1024 × 768` — Passive Non-Touch Stage)

- **64 px Status Bar:** Unified `--font-mono` / `--t-sub` neutral typography — Play/Pause status icon, queue position (`01 / 12`), and format (`FLAC 96kHz`) on left; Starred icon (when current track is starred), monochrome Battery icon, and Clock on right.
- **3 Stages (Unified Frameless Progress Bar pixel-locked at `left/right/bottom: 24px` across all 3 views):**
  1. **`COVER`:** Unobstructed `544×544 px` album art (zero badges over artwork) paired with vertically centered `--t-display` title, `--t-title` artist, and `--t-body` album above the shared progress bar + timecodes.
  2. **`VU`:** Dual backlit `478×540 px` analog VU meters (or segmented VFD dot-matrix meters) above the shared song title and progress bar.
  3. **`SPEC`:** 28 chunky FFT bars + stereo oscilloscope (`1024×704 px`) above the shared song title and progress bar.

---

## 4. Bottom Screen (`1024 × 768` — Option 1 Bounded Bar, Zero Redundant Footer)

Zero double headers and **zero bottom tooltip bar** (every screen gets the full `692 px` viewport height!). Subtle inline hardware keycap badges (`--t-cap`) are stamped only where helpful (`X` on Stage, `Y` on `⋯` Context Menu, `SEL` on `⚙` Settings — hero `Play/Pause`, `Prev`, and `Next` stay cleanly unbadged).

```
[L1]  [ ▶ DECK ]  [💿]  [👤]  [♫]  [♪]  [≡]  [R1]
```

1. **`DECK`:** Waveform scrubber + `⋯ [Y]` Context Menu + `⚙ [SEL]` Settings; 4 pure icon-only hero transport pads (`Prev`, `Play/Pause` / `Shuffle All` when idle, `Next`, `Star`); single-line volume bar (`🔊 Mute toggle` | `Slider` with pop-up value | `Output Device` menu); 4 quick pads (`Stage`, `Shuffle`, `Repeat`, `Album`).
2. **`ALBUMS` / `ARTISTS` / `PLAYLISTS` / `SONGS` (`-1` Pull/Press-Up Utility Bar):**
   - **Hidden by default** so 100% of the `692 px` viewport displays content with zero redundant category title text (the active expanding top tab already names the view).
   - **Press `D-Pad UP` from the top row** or **scroll/pull up at the top of the list** to smoothly reveal the `-1` utility bar containing only the **item count** (`100`), icon-only **Shuffle Play** & **Play** buttons (text revealed only when cursor-focused), a **Grid / List view toggle** (on `ALBUMS`, persisted in preferences), and a single **Order button** (`Added Time ↓`, opening one unified **Order By** sheet with both `ORDER BY` and context-clear `DIRECTION` — or `＋ New Playlist` on Playlists). Scrolling back down or pressing `DOWN` / `B` tucks it cleanly away.
   - **Detail Headers (Album, Artist, Playlist):** Icon-only **Shuffle Play**, **Play**, and **Add to Queue** buttons that smoothly reveal their text label only when focused by the keypad cursor.
   - **Album Detail & Rich `Get Info` Panel:** Album track rows stay clean and single-line (track number, title, duration, `⋯` — no redundant format/sample-rate clutter per row). Selecting **`Get Info`** from any track's Context Menu (`⋯` or `Y`) opens a scrollable, keypad-operable spec sheet displaying Album, Artist, Year, Disc/Track, Duration, Codec/Format, Sample Rate & Bit Depth, Bitrate, Channels, File Size, Added Date, ReplayGain, Signal Path, and MicroSD File Path.
3. **`QUEUE`:** True iTunes-style **Up Next** list containing only upcoming tracks (as each track starts playing it is removed from Up Next; `Prev` restores from played history; when Up Next is empty, `Next` stops cleanly). Features a dedicated **Play Next** icon button (moves track to top of Up Next) and **`⋯` / `Y`** Context Menu tailored to Queue (**Play Next**, **Go to Album**, **Go to Artist**, **Star/Unstar**, **Add to Playlist**, **Remove from Queue**).
4. **`Settings` (opened from `⚙` on `DECK`):** **7-Notch Screen Brightness Slider** (`15% → 100%`, live IPS dimming + detent chimes), **Visual Theme** (full-width), **Lid Close Hall Sensor**, and **Nintendo UI Sound FX**.

---

## 5. Native Appliance Runtime, GitHub OTA & LAN Debug / Web Control

- **Pure Native Dedicated DAP (Zero Web Engine on Device):**
  - Compiled `aarch64` native Linux binary on **64-bit Linux** (ROCKNIX kiosk mode initially, migrating to a minimal read-only Buildroot appliance image).
  - **Power on → straight into DECK DS**, supervised with auto-restart (`never exits` to an OS shell). Read-only OS rootfs + writable `/data` config/SQLite DB + `/mnt/sd2/Music` library.
  - Pre-fader audio metering (VU + Spectrum tied directly to source PCM before ALSA volume/mute), hardware `SW_LID` Pocket Audio mode, `KEY_POWER` graceful shutdown, and USB Mass Storage / BlueZ pairing inside Settings.
- **Opt-In OTA Updates (Empty by Default, Configured via LAN Web Config):**
  - **OTA URL is empty by default** — DeckDS never phones home or forces checking any hardcoded repository or server unless the user explicitly sets an update URL (e.g., a GitHub Releases endpoint such as `rGnng/DeckDS`, a personal fork, or a self-hosted LAN URL) in the **LAN Web Config** (or `config.toml` — no awkward URL typing on the handheld).
  - When an OTA URL is configured, DeckDS checks for new `aarch64` releases on demand (or optional periodic check), verifies the SHA-256 checksum, atomically swaps binary/assets into a staging slot (with automatic rollback on failure), and restarts `deckds`.
- **LAN Debug, Config & Remote Web Control (Built-In Lightweight HTTP/WS Server):**
  - Exposes a local LAN port (e.g., `http://<device-ip>:8080`, toggleable in Settings — runs natively on the device for external browsers on phone/PC, zero browser on the handheld).
  - **Debug & Telemetry:** Live log stream, audio pipeline / ALSA / sample-rate status, DRM/input event monitor, CPU/RAM/battery stats, and one-click OTA / binary upload during hardware development.
  - **Remote Control & Config:** Control playback, volume, queue, and library from any browser on the LAN, edit DeckDS settings / playlists, and trigger library rescan or Wi-Fi/BT configuration comfortably from a desktop keyboard.
