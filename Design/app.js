/* ==========================================================================
   DECK DS v8 — Option 1 Bounded Bar, 100 Default Albums, Notched Brightness,
   Play-Next Queue, Full Nintendo DS Keypad-Only + Touch Parity
   ========================================================================== */

(function () {
  const { DEMO_TRACKS, INITIAL_PLAYLISTS, generateWaveform, generateLargeLibraryTracks, DeckAudioEngine } = window.DECK_AUDIO;
  const audio = new DeckAudioEngine();

  const STORAGE_KEY = 'deckds_rgds_prefs_v2';
  const STAGES = ['cover', 'vu', 'spectrum'];
  const TOP_TABS = ['play', 'albums', 'artists', 'playlists', 'tracks', 'queue'];
  const BRIGHTNESS_NOTCHES = [15, 30, 45, 60, 75, 90, 100];
  const AUDIO_OUTPUTS = [
    { id: '3.5mm DAC', short: '3.5mm', label: '3.5mm Headphone DAC', icon: 'ic-headphones' },
    { id: 'SPEAKERS', short: 'Speakers', label: 'Built-in Stereo Speakers', icon: 'ic-speaker' },
    { id: 'BT 4.2', short: 'BT 4.2', label: 'Bluetooth 4.2 Audio', icon: 'ic-bluetooth' },
  ];

  const SORT_OPTIONS = {
    albums: [
      { field: 'added', label: 'Added Time', mode: 'number', asc: '↑ Oldest First', desc: '↓ Newest First' },
      { field: 'name', label: 'Name', mode: 'alpha', asc: '↑ A → Z', desc: '↓ Z → A' },
      { field: 'year', label: 'Year', mode: 'year', asc: '↑ Oldest First', desc: '↓ Newest First' },
      { field: 'artist', label: 'Artist', mode: 'alpha', asc: '↑ A → Z', desc: '↓ Z → A' },
    ],
    tracks: [
      { field: 'added', label: 'Added Time', mode: 'number', asc: '↑ Oldest First', desc: '↓ Newest First' },
      { field: 'name', label: 'Name', mode: 'alpha', asc: '↑ A → Z', desc: '↓ Z → A' },
      { field: 'year', label: 'Year', mode: 'year', asc: '↑ Oldest First', desc: '↓ Newest First' },
      { field: 'artist', label: 'Artist', mode: 'alpha', asc: '↑ A → Z', desc: '↓ Z → A' },
      { field: 'album', label: 'Album', mode: 'alpha', asc: '↑ A → Z', desc: '↓ Z → A' },
    ],
    artists: [
      { field: 'name', label: 'Name', mode: 'alpha', asc: '↑ A → Z', desc: '↓ Z → A' },
      { field: 'albums', label: 'Album Count', mode: 'number', asc: '↑ Fewest First', desc: '↓ Most First' },
      { field: 'tracks', label: 'Song Count', mode: 'number', asc: '↑ Fewest First', desc: '↓ Most First' },
    ],
  };

  // Play Deck 2D keypad focus map:
  // Row 0: [Context Y, Settings SEL], Row 1: [Waveform], Row 2: [Prev, Play, Next, Star],
  // Row 3: [Volume Slider, Output Menu Btn], Row 4: [Stage X, Shuffle, Repeat, Album]
  const PLAY_FOCUS_COLS = [2, 1, 4, 2, 4];

  function loadSavedPrefs() {
    try {
      const raw = localStorage.getItem(STORAGE_KEY);
      return raw ? JSON.parse(raw) : {};
    } catch (_) {
      return {};
    }
  }

  const saved = loadSavedPrefs();
  const sim2kInit = !!saved.sim2k;

  // Default = 100 rich fake albums; optional 2,000-album stress sim toggle
  function buildTrackList(use2k) {
    const base = generateLargeLibraryTracks(use2k ? 2000 : 100);
    if (Array.isArray(saved.starredIds)) {
      const starSet = new Set(saved.starredIds);
      base.forEach((t) => { t.starred = starSet.has(t.id); });
    }
    return base;
  }

  const initialTracks = buildTrackList(sim2kInit);
  const initialPlaylists = Array.isArray(saved.playlists)
    ? saved.playlists.map((p) => ({ ...p, trackIds: [...(p.trackIds || [])] }))
    : INITIAL_PLAYLISTS.map((p) => ({ ...p, trackIds: [...p.trackIds] }));

  const defaultSortPrefs = {
    albums: { field: 'added', dir: 'desc' },
    tracks: { field: 'added', dir: 'desc' },
    artists: { field: 'name', dir: 'asc' },
  };
  const savedSort = saved.sortPrefs || {};

  const initCurIdx = Math.max(0, initialTracks.findIndex((t) => t.id === saved.currentTrackId));
  const state = {
    sim2k: sim2kInit,
    tracks: initialTracks,
    playlists: initialPlaylists,
    history: [],
    queue: initialTracks.slice(initCurIdx + 1, initCurIdx + 12).map((t) => t.id),
    currentTrackId: initialTracks[initCurIdx].id,
    stage: STAGES.includes(saved.stage) ? saved.stage : 'cover',
    tab: TOP_TABS.includes(saved.tab) ? saved.tab : 'play',
    inSettings: false,
    brightnessIdx: typeof saved.brightnessIdx === 'number' ? Math.max(0, Math.min(6, saved.brightnessIdx)) : 6,
    lidClosed: false,
    lidMode: ['pocket', 'pause'].includes(saved.lidMode) ? saved.lidMode : 'pocket',
    sfxEnabled: typeof saved.sfxEnabled === 'boolean' ? saved.sfxEnabled : true,
    theme: ['obsidian', 'ceramic', 'vfd'].includes(saved.theme) ? saved.theme : 'obsidian',
    view: ['clamshell', 'screens', 'physical'].includes(saved.view) ? saved.view : 'clamshell',
    integerScale: !!saved.integerScale,
    volume: typeof saved.volume === 'number' ? Math.max(0, Math.min(100, saved.volume)) : 76,
    muted: !!saved.muted,
    lastNonZeroVol: typeof saved.volume === 'number' && saved.volume > 0 ? saved.volume : 76,
    shuffle: !!saved.shuffle,
    repeat: ['all', 'one', 'off'].includes(saved.repeat) ? saved.repeat : 'off',
    output: ['3.5mm DAC', 'SPEAKERS', 'BT 4.2'].includes(saved.output) ? saved.output : '3.5mm DAC',

    sortPrefs: {
      albums: savedSort.albums || defaultSortPrefs.albums,
      tracks: savedSort.tracks || defaultSortPrefs.tracks,
      artists: savedSort.artists || defaultSortPrefs.artists,
    },
    albumView: ['grid', 'list'].includes(saved.albumView) ? saved.albumView : 'grid',

    // Detail drill-down stack per session (e.g. [{ type: 'album', id }, { type: 'artist', id }, { type: 'playlist', id }])
    detailStack: [],

    // Keypad Focus Cursors (100% Keypad Operable!)
    playFocusRow: 2,
    playFocusCol: 1, // starts on hero Play/Pause
    listFocusIdx: 0,
    minusOneOpen: false, // -1 viewport utility bar (count + search + play/shuffle + album view + order / new playlist)
    inMinusOne: false,
    minusOneFocusIdx: 0,
    searchOpen: false,
    searchQuery: '',
    oskOpen: false,
    oskTargetId: null,
    oskMode: 'abc',
    oskRow: 0,
    oskCol: 0,
    detailFocusIdx: 0,   // -3 = Shuffle Play, -2 = Play, -1 = Queue, 0..N = track rows
    queueFocusIdx: 0,
    settingsFocusRow: 0, // 0=Brightness, 1=Theme, 2=Lid, 3=SFX, 4=TopDisplay
    sheetFocusIdx: 0,

    // Active Sorted Items + Scrubber Cache
    activeSortedItems: [],
    activeDetailAlbums: [],
    activeDetailTracks: [],
    activeScrubContext: null,

    // Context Menu / Picker State
    actionCtx: null, // { trackId, source: 'catalog'|'queue'|'playlist'|'play', playlistId?, queueIdx? }
    pickerTrackId: null,

    vuNeedleL: 0,
    vuNeedleR: 0,
    vuPeakL: 0,
    vuPeakR: 0,
    specPeaks: new Float32Array(28),
  };

  let cachedAlbums = null;
  let cachedArtists = null;
  let trackByIdMap = new Map();

  function rebuildIndexes() {
    trackByIdMap = new Map();
    state.tracks.forEach((t) => trackByIdMap.set(t.id, t));
    cachedAlbums = null;
    cachedArtists = null;
  }
  rebuildIndexes();

  function savePrefs() {
    try {
      const payload = {
        sim2k: state.sim2k,
        view: state.view,
        integerScale: state.integerScale,
        theme: state.theme,
        stage: state.stage,
        tab: state.tab,
        brightnessIdx: state.brightnessIdx,
        lidMode: state.lidMode,
        sfxEnabled: state.sfxEnabled,
        volume: state.volume,
        muted: state.muted,
        shuffle: state.shuffle,
        repeat: state.repeat,
        output: state.output,
        sortPrefs: state.sortPrefs,
        albumView: state.albumView,
        currentTrackId: state.currentTrackId,
        starredIds: state.tracks.filter((t) => t.starred).map((t) => t.id),
        playlists: state.playlists,
      };
      localStorage.setItem(STORAGE_KEY, JSON.stringify(payload));
    } catch (_) {}
  }

  /* ------------------------------------------------------------------------
     MOUNT TEMPLATES INTO EXACT 1024×768 SLOTS
     ------------------------------------------------------------------------ */
  const slotTop = document.getElementById('slotTop');
  const slotBottom = document.getElementById('slotBottom');
  const stageRoot = document.getElementById('tplStageScreen').content.firstElementChild.cloneNode(true);
  const consoleRoot = document.getElementById('tplConsoleScreen').content.firstElementChild.cloneNode(true);

  slotTop.appendChild(stageRoot);
  slotBottom.appendChild(consoleRoot);

  const $s = (sel) => stageRoot.querySelector(sel);
  const $c = (sel) => consoleRoot.querySelector(sel);
  const $$c = (sel) => Array.from(consoleRoot.querySelectorAll(sel));

  /* ------------------------------------------------------------------------
     VIEWPORT & APPLE STUDIO DISPLAY 27" 5K TRUE 4.3" CALIBRATION
     ------------------------------------------------------------------------ */
  const scalerEl = document.getElementById('deviceScaler');
  const deviceEl = document.getElementById('rgdsDevice');
  const workspaceStage = document.getElementById('workspaceStage');

  const rulerNoteEl = document.getElementById('physicalRulerNote');

  function updateIntegerScaleUI(physRatio = 1, cssScale = 1, dpr = 1) {
    const btn = document.getElementById('btnIntegerScale');
    if (!btn) return;
    btn.classList.toggle('active', state.integerScale);
    btn.textContent = state.integerScale ? `Integer ${physRatio}:1` : 'Integer Scale';
    document.documentElement.setAttribute('data-integer', String(state.integerScale));
    workspaceStage.classList.toggle('integer-mode', state.integerScale);
    if (state.integerScale) {
      rulerNoteEl.textContent = `INTEGER SCALE LOCKED — ${physRatio}:1 PHYSICAL PIXELS (${cssScale}× CSS @ ${dpr}× DPR) — ZERO FRACTIONAL RESAMPLING`;
    } else {
      rulerNoteEl.textContent = 'APPLE STUDIO DISPLAY 27" 5K (109 CSS PPI) — EACH 4.3" SCREEN = 375 × 281 CSS px (EXACT 87.4 × 65.5 mm)';
    }
  }

  function fitDeviceToViewport() {
    const pad = (state.view === 'physical' || state.integerScale) ? 68 : 28;
    const availW = workspaceStage.clientWidth - pad;
    const availH = workspaceStage.clientHeight - pad;
    const natW = deviceEl.offsetWidth || 1500;
    const natH = deviceEl.offsetHeight || 1700;
    const fitScale = Math.min(availW / natW, availH / natH, 1);

    if (state.integerScale) {
      const dpr = Math.max(1, Math.round(window.devicePixelRatio || 1));
      const physRatio = Math.max(1, Math.floor(fitScale * dpr));
      const intScale = physRatio / dpr;
      scalerEl.style.transform = `scale(${intScale})`;
      updateIntegerScaleUI(physRatio, intScale, dpr);
    } else if (state.view === 'physical') {
      const studio5kScale = (3.44 * 109) / 1024;
      scalerEl.style.transform = `scale(${Math.min(studio5kScale, fitScale).toFixed(4)})`;
      updateIntegerScaleUI();
    } else {
      scalerEl.style.transform = `scale(${fitScale.toFixed(4)})`;
      updateIntegerScaleUI();
    }
  }
  window.addEventListener('resize', fitDeviceToViewport);

  function toggleIntegerScale() {
    state.integerScale = !state.integerScale;
    if (state.integerScale && state.view === 'physical') {
      state.view = 'screens';
      document.documentElement.setAttribute('data-view', state.view);
      document.querySelectorAll('[data-set-view]').forEach((b) => {
        b.classList.toggle('active', b.dataset.setView === state.view);
      });
    }
    sfx('tick');
    savePrefs();
    requestAnimationFrame(() => {
      fitDeviceToViewport();
      showToast(state.integerScale ? 'Integer Pixel Scale ON' : 'Integer Pixel Scale OFF');
    });
  }

  /* ------------------------------------------------------------------------
     HELPERS, BRIGHTNESS & CATALOG SORTING
     ------------------------------------------------------------------------ */
  let splitHandledVoice = null;
  let splitHandledTs = 0;

  function sfx(type) {
    if (!state.sfxEnabled) return;
    // Skip duplicate full chime if it was just played as a split press-down / release pair!
    if (splitHandledVoice && performance.now() - splitHandledTs < 65) {
      if (type === splitHandledVoice || (splitHandledVoice === 'press' && type === 'tick')) {
        splitHandledVoice = null;
        return;
      }
    }
    audio.playSfx(type);
  }

  function fmtTime(sec) {
    const s = Math.max(0, Math.floor(sec || 0));
    return `${String(Math.floor(s / 60)).padStart(2, '0')}:${String(s % 60).padStart(2, '0')}`;
  }

  function getTrackById(id) {
    return trackByIdMap.get(id) || state.tracks[0];
  }

  function getCurrentTrack() {
    return state.currentTrackId ? getTrackById(state.currentTrackId) : null;
  }

  function applyScreenBrightness() {
    const pct = BRIGHTNESS_NOTCHES[state.brightnessIdx] || 100;
    const factor = 0.28 + (pct / 100) * 0.72; // visually realistic IPS backlight curve
    slotBottom.style.filter = `brightness(${factor.toFixed(2)})`;
    slotTop.style.filter = `brightness(${factor.toFixed(2)})`;
    $c('#brightnessSlider').value = String(state.brightnessIdx);
    $c('#brightnessVal').textContent = `${pct}%`;
  }

  function setBrightnessIndex(idx, silent = false) {
    const clamped = Math.max(0, Math.min(BRIGHTNESS_NOTCHES.length - 1, idx));
    if (clamped === state.brightnessIdx && !silent) {
      sfx('bump');
      return;
    }
    state.brightnessIdx = clamped;
    applyScreenBrightness();
    savePrefs();
    if (!silent) {
      sfx('tick');
      showToast(`Brightness: ${BRIGHTNESS_NOTCHES[clamped]}%`);
    }
  }

  function getAlbums() {
    if (cachedAlbums) return cachedAlbums;
    const map = new Map();
    state.tracks.forEach((t) => {
      if (!map.has(t.albumId)) {
        map.set(t.albumId, {
          id: t.albumId,
          title: t.album,
          artist: t.artist,
          year: t.year || 2025,
          addedAt: t.addedAt || '2026-01-01',
          artStyle: t.artStyle,
          colors: t.colors,
          tracks: [],
        });
      }
      const alb = map.get(t.albumId);
      alb.tracks.push(t);
      if (t.addedAt && t.addedAt > alb.addedAt) alb.addedAt = t.addedAt;
    });
    map.forEach((alb) => {
      alb.tracks.sort((a, b) => ((a.discNo || 1) - (b.discNo || 1)) || ((a.trackNo || 99) - (b.trackNo || 99)));
      const discs = new Set(alb.tracks.map((t) => t.discNo || 1));
      alb.discCount = discs.size;
    });
    cachedAlbums = Array.from(map.values());
    return cachedAlbums;
  }

  function getAlbumById(albumId) {
    return getAlbums().find((a) => a.id === albumId);
  }

  function getArtists() {
    if (cachedArtists) return cachedArtists;
    const map = new Map();
    state.tracks.forEach((t) => {
      if (!map.has(t.artist)) {
        const initials = t.artist
          .split(/\s+/)
          .filter(Boolean)
          .slice(0, 2)
          .map((w) => w[0].toUpperCase())
          .join('');
        map.set(t.artist, {
          name: t.artist,
          initials,
          colors: t.colors || ['#ff7a1a', '#1b2234', '#f2efe9'],
          albumIds: new Set(),
          tracks: [],
        });
      }
      const entry = map.get(t.artist);
      entry.albumIds.add(t.albumId);
      entry.tracks.push(t);
    });
    const albumLookup = new Map(getAlbums().map((a) => [a.id, a]));
    cachedArtists = Array.from(map.values()).map((a) => ({
      ...a,
      albums: Array.from(a.albumIds).map((id) => albumLookup.get(id)).filter(Boolean),
    }));
    return cachedArtists;
  }

  function getArtistByName(name) {
    return getArtists().find((a) => a.name === name);
  }

  function getStarredTrackIds() {
    return state.tracks.filter((t) => t.starred).map((t) => t.id);
  }

  function getSortedItems(category) {
    const pref = state.sortPrefs[category] || { field: 'name', dir: 'asc' };
    const mul = pref.dir === 'desc' ? -1 : 1;

    if (category === 'albums') {
      const list = [...getAlbums()];
      list.sort((a, b) => {
        if (pref.field === 'added') return a.addedAt.localeCompare(b.addedAt) * mul;
        if (pref.field === 'year') return ((a.year - b.year) || a.title.localeCompare(b.title)) * mul;
        if (pref.field === 'artist') return (a.artist.localeCompare(b.artist) || a.title.localeCompare(b.title)) * mul;
        return a.title.localeCompare(b.title) * mul;
      });
      return list;
    }

    if (category === 'tracks') {
      const list = [...state.tracks];
      list.sort((a, b) => {
        if (pref.field === 'added') return (a.addedAt || '').localeCompare(b.addedAt || '') * mul;
        if (pref.field === 'year') return (((a.year || 0) - (b.year || 0)) || a.title.localeCompare(b.title)) * mul;
        if (pref.field === 'artist') return (a.artist.localeCompare(b.artist) || a.title.localeCompare(b.title)) * mul;
        if (pref.field === 'album') return (a.album.localeCompare(b.album) || (a.trackNo || 0) - (b.trackNo || 0)) * mul;
        return a.title.localeCompare(b.title) * mul;
      });
      return list;
    }

    if (category === 'artists') {
      const list = [...getArtists()];
      list.sort((a, b) => {
        if (pref.field === 'albums') return ((a.albums.length - b.albums.length) || a.name.localeCompare(b.name)) * mul;
        if (pref.field === 'tracks') return ((a.tracks.length - b.tracks.length) || a.name.localeCompare(b.name)) * mul;
        return a.name.localeCompare(b.name) * mul;
      });
      return list;
    }

    return [];
  }

  function getSortOptionMeta(category) {
    const pref = state.sortPrefs[category] || { field: 'name', dir: 'asc' };
    const opts = SORT_OPTIONS[category] || [];
    const meta = opts.find((o) => o.field === pref.field) || opts[0];
    return { ...pref, ...meta };
  }

  function getThemeColors() {
    const cs = getComputedStyle(document.documentElement);
    return {
      bg: cs.getPropertyValue('--bg').trim(),
      bgElev: cs.getPropertyValue('--bg-elev').trim(),
      bgCard: cs.getPropertyValue('--bg-card').trim(),
      border: cs.getPropertyValue('--border').trim(),
      borderStrong: cs.getPropertyValue('--border-strong').trim(),
      text: cs.getPropertyValue('--text').trim(),
      textDim: cs.getPropertyValue('--text-dim').trim(),
      textMuted: cs.getPropertyValue('--text-muted').trim(),
      accent: cs.getPropertyValue('--accent').trim(),
      mint: cs.getPropertyValue('--mint').trim(),
      danger: cs.getPropertyValue('--danger').trim(),
      vuBg: cs.getPropertyValue('--vu-bg').trim(),
      vuGlow: cs.getPropertyValue('--vu-glow').trim(),
      wfInactive: cs.getPropertyValue('--wf-inactive').trim(),
    };
  }

  let toastTimer = null;
  function showToast(label) {
    const toast = document.getElementById('hapticToast');
    toast.textContent = label;
    toast.classList.add('show');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => toast.classList.remove('show'), 1400);
  }

  /* ------------------------------------------------------------------------
     GENERATIVE ALBUM ART & ARTIST EMBLEM
     ------------------------------------------------------------------------ */
  function drawAlbumArt(canvas, item) {
    const ctx = canvas.getContext('2d');
    const w = canvas.width;
    const h = canvas.height;
    ctx.clearRect(0, 0, w, h);

    if (!item) {
      const tc = getThemeColors();
      ctx.fillStyle = tc.bgCard;
      ctx.fillRect(0, 0, w, h);
      ctx.strokeStyle = tc.border;
      ctx.lineWidth = Math.max(2, Math.round(w * 0.008));
      ctx.beginPath();
      ctx.arc(w * 0.5, h * 0.5, w * 0.24, 0, Math.PI * 2);
      ctx.stroke();
      ctx.beginPath();
      ctx.arc(w * 0.5, h * 0.5, w * 0.06, 0, Math.PI * 2);
      ctx.stroke();
      return;
    }

    const [c1, c2, c3] = item.colors || ['#ff7a1a', '#161a24', '#f2efe9'];
    const style = item.artStyle || 'bauhaus-amber';

    const bg = ctx.createLinearGradient(0, 0, w, h);
    bg.addColorStop(0, c2);
    bg.addColorStop(1, '#08090c');
    ctx.fillStyle = bg;
    ctx.fillRect(0, 0, w, h);

    ctx.save();
    if (style === 'bauhaus-amber') {
      ctx.fillStyle = c1;
      ctx.beginPath();
      ctx.arc(w * 0.56, h * 0.48, w * 0.34, 0, Math.PI * 2);
      ctx.fill();
      ctx.fillStyle = c3;
      ctx.fillRect(w * 0.12, h * 0.62, w * 0.44, h * 0.24);
      ctx.strokeStyle = c2;
      ctx.lineWidth = w * 0.048;
      ctx.beginPath();
      ctx.arc(w * 0.56, h * 0.48, w * 0.18, 0, Math.PI * 2);
      ctx.stroke();
      ctx.fillStyle = c2;
      ctx.fillRect(w * 0.12, h * 0.14, w * 0.14, h * 0.34);
    } else if (style === 'vinyl-sage') {
      for (let r = w * 0.44; r > w * 0.12; r -= w * 0.036) {
        ctx.strokeStyle = r % 2 === 0 ? c1 : 'rgba(255,255,255,0.14)';
        ctx.lineWidth = Math.max(2, w * 0.005);
        ctx.beginPath();
        ctx.arc(w * 0.5, h * 0.5, r, 0, Math.PI * 2);
        ctx.stroke();
      }
      ctx.fillStyle = c1;
      ctx.beginPath();
      ctx.arc(w * 0.5, h * 0.5, w * 0.18, 0, Math.PI * 2);
      ctx.fill();
      ctx.fillStyle = c2;
      ctx.beginPath();
      ctx.arc(w * 0.5, h * 0.5, w * 0.045, 0, Math.PI * 2);
      ctx.fill();
    } else if (style === 'vfd-grid') {
      ctx.strokeStyle = 'rgba(56,242,198,0.24)';
      ctx.lineWidth = Math.max(1.5, w * 0.004);
      const step = w / 10;
      for (let i = 0; i <= 10; i++) {
        ctx.beginPath(); ctx.moveTo(i * step, 0); ctx.lineTo(i * step, h); ctx.stroke();
        ctx.beginPath(); ctx.moveTo(0, i * step); ctx.lineTo(w, i * step); ctx.stroke();
      }
      ctx.fillStyle = c1;
      for (let i = 0; i < 7; i++) {
        const bh = h * (0.2 + Math.abs(Math.sin(i * 0.9)) * 0.5);
        ctx.fillRect(w * 0.14 + i * w * 0.108, h * 0.84 - bh, w * 0.072, bh);
      }
      ctx.fillStyle = c3;
      ctx.fillRect(w * 0.14, h * 0.15, w * 0.72, h * 0.038);
    } else if (style === 'braun-swiss') {
      ctx.fillStyle = c2;
      ctx.fillRect(0, 0, w, h);
      ctx.fillStyle = c3;
      for (let gx = 0; gx < 6; gx++) {
        for (let gy = 0; gy < 6; gy++) {
          ctx.beginPath();
          ctx.arc(w * 0.18 + gx * w * 0.064, h * 0.18 + gy * h * 0.064, w * 0.015, 0, Math.PI * 2);
          ctx.fill();
        }
      }
      ctx.fillStyle = c1;
      ctx.beginPath();
      ctx.arc(w * 0.68, h * 0.66, w * 0.21, 0, Math.PI * 2);
      ctx.fill();
      ctx.fillStyle = c3;
      ctx.fillRect(w * 0.14, h * 0.78, w * 0.34, h * 0.045);
    } else if (style === 'starred-badge') {
      ctx.fillStyle = '#1b1610';
      ctx.fillRect(0, 0, w, h);
      ctx.fillStyle = '#ff7a1a';
      ctx.font = `700 ${Math.round(w * 0.56)}px sans-serif`;
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      ctx.fillText('★', w / 2, h / 2 + 2);
    } else {
      ctx.fillStyle = '#141923';
      ctx.fillRect(0, 0, w, h);
      ctx.fillStyle = c1;
      ctx.font = `700 ${Math.round(w * 0.45)}px "IBM Plex Mono", monospace`;
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      ctx.fillText('♫', w / 2, h / 2 + 2);
    }
    ctx.restore();
  }

  function drawArtistEmblem(canvas, artist) {
    const ctx = canvas.getContext('2d');
    const w = canvas.width;
    const h = canvas.height;
    const [c1, c2] = artist.colors || ['#ff7a1a', '#1b2234'];
    ctx.clearRect(0, 0, w, h);
    const grad = ctx.createLinearGradient(0, 0, w, h);
    grad.addColorStop(0, c1);
    grad.addColorStop(1, c2);
    ctx.fillStyle = grad;
    ctx.fillRect(0, 0, w, h);
    ctx.fillStyle = '#f5f2ea';
    ctx.font = `700 ${Math.round(w * 0.42)}px "Space Grotesk", sans-serif`;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(artist.initials || '?', w / 2, h / 2 + 2);
  }

  /* ------------------------------------------------------------------------
     TRACK LOADING, STARRING & ITUNES-STYLE UP NEXT QUEUE LOGIC
     ------------------------------------------------------------------------ */
  function loadCurrentTrack(trackId, autoPlay = true) {
    if (!trackId) return;
    const trk = getTrackById(trackId);
    state.currentTrackId = trk.id;
    audio.loadTrack(trk, autoPlay);
    savePrefs();
    syncTrackUI();
    renderActiveView();
    updatePlayStateUI();
  }

  function selectTrack(trackId, autoPlay = true, replaceQueueIds = null) {
    if (!trackId) return;
    if (replaceQueueIds && replaceQueueIds.length) {
      const pos = Math.max(0, replaceQueueIds.indexOf(trackId));
      state.history = pos > 0 ? replaceQueueIds.slice(0, pos) : [];
      state.queue = replaceQueueIds.slice(pos + 1);
      state.queueFocusIdx = 0;
    } else if (state.currentTrackId && state.currentTrackId !== trackId) {
      state.history.push(state.currentTrackId);
    }
    loadCurrentTrack(trackId, autoPlay);
  }

  // Jump directly to a song in Up Next: plays it and removes it (and skipped tracks ahead of it) from Up Next
  function playFromQueue(queueIdx) {
    if (queueIdx < 0 || queueIdx >= state.queue.length) return;
    if (state.currentTrackId) state.history.push(state.currentTrackId);
    for (let i = 0; i < queueIdx; i++) state.history.push(state.queue[i]);
    const targetId = state.queue[queueIdx];
    state.queue.splice(0, queueIdx + 1);
    state.queueFocusIdx = Math.min(state.queueFocusIdx, Math.max(0, state.queue.length - 1));
    loadCurrentTrack(targetId, true);
  }

  // Play Next: moves or inserts track to the very top of Up Next!
  function playNextInQueue(trackId, existingQueueIdx = -1) {
    if (existingQueueIdx >= 0) {
      if (existingQueueIdx === 0) {
        showToast('Already Playing Next');
        return;
      }
      state.queue.splice(existingQueueIdx, 1);
    }
    state.queue.unshift(trackId);
    state.queueFocusIdx = 0;
    sfx('add');
    syncTrackUI();
    renderQueue();
    showToast(`Play Next: ${getTrackById(trackId).title}`);
  }

  function removeFromQueue(queueIdx) {
    if (queueIdx < 0 || queueIdx >= state.queue.length) return;
    const removed = getTrackById(state.queue[queueIdx]);
    state.queue.splice(queueIdx, 1);
    if (state.queueFocusIdx >= state.queue.length) {
      state.queueFocusIdx = Math.max(0, state.queue.length - 1);
    }
    sfx('back');
    syncTrackUI();
    renderQueue();
    showToast(state.queue.length === 0 ? 'Up Next Empty' : `Removed: ${removed.title}`);
  }

  function stopPlaybackToIdle() {
    if (state.currentTrackId) state.history.push(state.currentTrackId);
    state.currentTrackId = null;
    audio.pause();
    audio.currentTime = 0;
    audio.duration = 0;
    savePrefs();
    syncTrackUI();
    updatePlayStateUI();
    drawWaveform();
    renderActiveView();
  }

  function toggleStarTrack(trackId) {
    const id = trackId || state.currentTrackId;
    if (!id) { sfx('bump'); return; }
    const trk = getTrackById(id);
    trk.starred = !trk.starred;
    sfx(trk.starred ? 'star' : 'unstar');
    savePrefs();
    syncTrackUI();
    renderActiveView();
    showToast(trk.starred ? `★ Starred: ${trk.title}` : `☆ Unstarred: ${trk.title}`);
  }

  /* ------------------------------------------------------------------------
     OPTION 1 BOUNDED TAB NAVIGATION + SETTINGS OVERLAY ON PLAY
     ------------------------------------------------------------------------ */
  function isCatalogTab(tab) {
    return tab === 'albums' || tab === 'artists' || tab === 'playlists' || tab === 'tracks';
  }

  function setTab(tabName, silent = false) {
    if (!TOP_TABS.includes(tabName)) return;
    if (!silent && (state.tab !== tabName || state.inSettings)) sfx('deck');
    state.tab = tabName;
    state.inSettings = false;
    state.detailStack = [];
    state.listFocusIdx = 0;
    state.minusOneOpen = false;
    state.inMinusOne = false;
    state.searchOpen = false;
    state.searchQuery = '';
    closeOsk(false);
    const searchInput = $c('#catSearchInput');
    if (searchInput) { searchInput.value = ''; searchInput.blur(); }
    state.queueFocusIdx = 0;
    $c('#libItems').scrollTop = 0;

    updateTabBarUI();
    showActivePanel();
    savePrefs();
    renderActiveView();
  }

  function openSettings() {
    sfx('confirm');
    closeAllSheets(false);
    state.inSettings = true;
    state.settingsFocusRow = 0;
    showActivePanel();
    syncSettingsFocusUI();
  }

  function closeSettings() {
    sfx('back');
    state.inSettings = false;
    showActivePanel();
    syncPlayFocusUI();
  }

  function updateTabBarUI() {
    const idx = TOP_TABS.indexOf(state.tab);
    $$c('.deck-tab').forEach((t) => t.classList.toggle('active', t.dataset.deck === state.tab));
    $c('#navBtnL1').classList.toggle('at-edge', idx <= 0);
    $c('#navBtnR1').classList.toggle('at-edge', idx >= TOP_TABS.length - 1);
  }

  function showActivePanel() {
    const panelKey = state.inSettings
      ? 'settings'
      : isCatalogTab(state.tab)
        ? 'catalog'
        : state.tab;

    $$c('.deck-panel').forEach((p) => p.classList.toggle('active', p.dataset.deck === panelKey));
  }

  function renderActiveView() {
    if (state.inSettings) {
      syncSettingsFocusUI();
    } else if (state.tab === 'play') {
      syncPlayFocusUI();
    } else if (state.tab === 'queue') {
      renderQueue();
    } else {
      renderCatalog();
    }
  }

  function pushDetail(detailObj) {
    sfx('confirm');
    state.detailStack.push(detailObj);
    state.detailFocusIdx = 0;
    $c('#libItems').scrollTop = 0;
    renderCatalog();
  }

  function popDetail() {
    if (!state.detailStack.length) return false;
    sfx('back');
    state.detailStack.pop();
    $c('#libItems').scrollTop = 0;
    renderCatalog();
    return true;
  }

  function goToAlbum(albumId) {
    const alb = getAlbumById(albumId);
    if (!alb) return;
    sfx('confirm');
    closeAllSheets(false);
    state.inSettings = false;
    if (state.tab !== 'albums' && state.tab !== 'artists') {
      state.tab = 'albums';
      state.detailStack = [{ type: 'album', id: alb.id }];
    } else {
      state.detailStack.push({ type: 'album', id: alb.id });
    }
    state.detailFocusIdx = 0;
    updateTabBarUI();
    showActivePanel();
    renderCatalog();
    showToast(`Album: ${alb.title}`);
  }

  function goToArtist(artistName) {
    const art = getArtistByName(artistName);
    if (!art) return;
    sfx('confirm');
    closeAllSheets(false);
    state.inSettings = false;
    state.tab = 'artists';
    state.detailStack = [{ type: 'artist', id: art.name }];
    state.detailFocusIdx = 0;
    updateTabBarUI();
    showActivePanel();
    renderCatalog();
    showToast(`Artist: ${art.name}`);
  }

  function toggle2kSim() {
    state.sim2k = !state.sim2k;
    sfx('confirm');
    state.tracks = buildTrackList(state.sim2k);
    rebuildIndexes();
    if (!trackByIdMap.has(state.currentTrackId)) {
      state.currentTrackId = state.tracks[0].id;
    }
    savePrefs();
    update2kButtonUI();
    renderActiveView();
    showToast(state.sim2k ? 'Loaded 2,000 Albums Sim' : 'Loaded 100 Albums Default');
  }

  function update2kButtonUI() {
    const btn = document.getElementById('btnToggle2k');
    btn.classList.toggle('active', state.sim2k);
    btn.textContent = state.sim2k ? '2,000 Albums: ON' : '2,000 Albums Sim';
  }

  /* ------------------------------------------------------------------------
     TRACK & STAGE UI SYNC
     ------------------------------------------------------------------------ */
  function syncOutputUI() {
    const out = AUDIO_OUTPUTS.find((o) => o.id === state.output) || AUDIO_OUTPUTS[0];
    $c('#outputBtnIcon').setAttribute('href', `#${out.icon}`);
    $c('#btnOpenOutputSheet').title = `Audio Output: ${out.label}`;
  }

  function openOutputSheet() {
    sfx('tick');
    state.sheetFocusIdx = Math.max(0, AUDIO_OUTPUTS.findIndex((o) => o.id === state.output));
    const list = $c('#outputDeviceList');
    list.innerHTML = '';

    AUDIO_OUTPUTS.forEach((out, idx) => {
      const isActive = out.id === state.output;
      const row = document.createElement('button');
      row.className = `picker-row${isActive ? ' member' : ''}${idx === state.sheetFocusIdx ? ' kp-focus' : ''}`;
      row.innerHTML = `
        <span style="display:inline-flex;align-items:center;gap:14px;">
          <svg class="ic-28"><use href="#${out.icon}"/></svg>
          <span>${out.label}</span>
        </span>
        <span class="picker-check">${isActive ? '✓ Active' : ''}</span>
      `;
      row.addEventListener('click', () => {
        state.output = out.id;
        sfx('confirm');
        savePrefs();
        syncOutputUI();
        closeAllSheets(false);
        showToast(`Output: ${out.short}`);
      });
      list.appendChild(row);
    });

    $c('#outputDeviceSheet').hidden = false;
  }

  function syncTopTextFadeMasks() {
    requestAnimationFrame(() => {
      ['#coverTitle', '#coverArtist', '#coverAlbum', '#stageNowTitle'].forEach((sel) => {
        const el = $s(sel);
        if (!el) return;
        el.classList.toggle('is-overflowing', el.scrollWidth > el.clientWidth + 1);
      });
    });
  }

  function syncTrackUI() {
    const trk = getCurrentTrack();
    const auxStarBtn = document.getElementById('hwBtnAux1');
    const auxStarGlyph = document.getElementById('hwAuxStarGlyph');

    if (!trk) {
      $s('#topCodecBadge').textContent = '';
      $s('#topQueuePos').textContent = '00 / 00';
      $s('#topStarIcon').hidden = true;
      drawAlbumArt($s('#albumArtCanvas'), null);
      $s('#coverAmbientGlow').style.background = 'none';

      $s('#coverTitle').textContent = '';
      $s('#coverArtist').textContent = '';
      $s('#coverAlbum').textContent = '';
      $s('#stageNowTitle').textContent = '';
      $s('#stageTimeCur').textContent = '00:00';
      $s('#stageTimeTot').textContent = '00:00';
      $s('#stageProgressFill').style.width = '0%';

      $c('#wfTrackTitle').textContent = '';
      $c('#timeCurrent').textContent = '00:00';
      $c('#timeTotal').textContent = '00:00';
      $c('#starSvgUse').setAttribute('href', '#ic-star');
      $c('#btnStar').classList.remove('active');
      if (auxStarBtn) auxStarBtn.classList.remove('starred');
      if (auxStarGlyph) auxStarGlyph.textContent = '☆';

      document.getElementById('pocketTrackTitle').textContent = '';
      document.getElementById('pocketTrackMeta').textContent = '';
      updatePlayStateUI();
      syncTopTextFadeMasks();
      return;
    }

    const totalSession = state.history.length + 1 + state.queue.length;
    const curPos = state.history.length + 1;

    $s('#topCodecBadge').textContent = `${trk.format.split(' ')[0]} ${trk.sampleRate.replace('.0', '')}`;
    $s('#topStarIcon').hidden = !trk.starred;

    drawAlbumArt($s('#albumArtCanvas'), trk);
    const glow = `radial-gradient(circle at 30% 45%, ${trk.colors[0]} 0%, ${trk.colors[1]} 55%, transparent 100%)`;
    $s('#coverAmbientGlow').style.background = glow;

    $s('#topQueuePos').textContent = `${String(curPos).padStart(2, '0')} / ${String(totalSession).padStart(2, '0')}`;
    $s('#coverTitle').textContent = trk.title;
    $s('#coverArtist').textContent = trk.artist;
    $s('#coverAlbum').textContent = trk.album;

    $s('#stageNowTitle').textContent = trk.title;
    $s('#stageTimeTot').textContent = fmtTime(audio.duration);

    $c('#wfTrackTitle').textContent = trk.title;
    $c('#timeTotal').textContent = fmtTime(audio.duration);
    $c('#starSvgUse').setAttribute('href', trk.starred ? '#ic-star-filled' : '#ic-star');
    $c('#btnStar').classList.toggle('active', !!trk.starred);
    if (auxStarBtn) auxStarBtn.classList.toggle('starred', !!trk.starred);
    if (auxStarGlyph) auxStarGlyph.textContent = trk.starred ? '★' : '☆';

    document.getElementById('pocketTrackTitle').textContent = trk.title;
    document.getElementById('pocketTrackMeta').textContent = `${trk.artist} / ${trk.album}`;
    syncTopTextFadeMasks();
  }

  function updatePlayStateUI() {
    const hasTrack = !!state.currentTrackId;
    const playing = hasTrack && audio.isPlaying;
    const shuffleAllMode = !hasTrack && state.queue.length === 0;
    const topPlay = $s('#topPlayState');
    topPlay.classList.toggle('is-playing', playing);
    topPlay.setAttribute('aria-label', !hasTrack ? 'Stopped' : playing ? 'Playing' : 'Paused');
    $s('#topPlaySvgUse').setAttribute('href', !hasTrack ? '#ic-stop' : playing ? '#ic-play' : '#ic-pause');
    $c('#playPauseSvgUse').setAttribute('href', shuffleAllMode ? '#ic-shuffle' : playing ? '#ic-pause' : '#ic-play');
    const heroBtn = $c('#btnPlayPause');
    if (heroBtn) {
      heroBtn.title = shuffleAllMode ? 'Shuffle All (START)' : playing ? 'Pause (START)' : 'Play (START)';
      heroBtn.setAttribute('aria-label', shuffleAllMode ? 'Shuffle All' : playing ? 'Pause' : 'Play');
    }
    document.getElementById('hingeAudioLed').classList.toggle('active', playing);
    document.getElementById('pocketPlayBtn').textContent = shuffleAllMode ? 'SHUFFLE' : playing ? '⏸ PAUSE' : '▶ PLAY';
  }

  function syncQuickPadsUI() {
    const stageIcons = { cover: '#ic-stage-cover', vu: '#ic-stage-vu', spectrum: '#ic-stage-spectrum' };
    $c('#stageSvgUse').setAttribute('href', stageIcons[state.stage] || '#ic-stage-cover');
    $c('#btnShuffle').classList.toggle('active', state.shuffle);
    $c('#btnRepeat').classList.toggle('active', state.repeat !== 'off');
    $c('#repeatSvgUse').setAttribute('href', state.repeat === 'one' ? '#ic-repeat-one' : '#ic-repeat');
  }

  function togglePlayWithSfx() {
    if (!state.currentTrackId) {
      if (state.queue.length > 0) {
        sfx('play');
        playFromQueue(0);
        showToast('Playing');
      } else if (state.tracks.length > 0) {
        const shuffled = shuffleTrackIds(state.tracks.map((t) => t.id));
        sfx('play');
        selectTrack(shuffled[0], true, shuffled);
        showToast('Shuffle All');
      } else {
        sfx('bump');
      }
      return;
    }
    const nowPlaying = audio.togglePlay();
    sfx(nowPlaying ? 'play' : 'pause');
    updatePlayStateUI();
    showToast(nowPlaying ? 'Playing' : 'Paused');
  }

  function nextTrack(fromAuto = false) {
    if (fromAuto && state.repeat === 'one' && state.currentTrackId) {
      audio.seek(0);
      audio.play();
      return;
    }
    if (state.queue.length === 0) {
      stopPlaybackToIdle();
      if (!fromAuto) sfx('bump');
      return;
    }
    const prevId = state.currentTrackId;
    if (prevId) state.history.push(prevId);
    let nextId;
    if (state.shuffle) {
      const rIdx = Math.floor(Math.random() * state.queue.length);
      nextId = state.queue.splice(rIdx, 1)[0];
    } else {
      nextId = state.queue.shift();
    }
    if (state.repeat === 'all' && prevId) {
      state.queue.push(prevId);
    }
    if (!nextId) return;
    if (!fromAuto) sfx('tick');
    loadCurrentTrack(nextId, true);
    showToast(`Next: ${getCurrentTrack().title}`);
  }

  function prevTrack() {
    if (state.currentTrackId && audio.currentTime > 3) {
      sfx('tick');
      audio.seek(0);
      showToast('Restart Track');
      return;
    }
    if (state.history.length === 0) {
      if (state.currentTrackId) {
        sfx('tick');
        audio.seek(0);
        showToast('Restart Track');
      } else {
        sfx('bump');
      }
      return;
    }
    sfx('tick');
    const prevId = state.history.pop();
    if (state.currentTrackId) state.queue.unshift(state.currentTrackId);
    loadCurrentTrack(prevId, true);
    showToast(`Prev: ${getCurrentTrack().title}`);
  }

  /* ------------------------------------------------------------------------
     STAGE, THEME, TOP POWER & LID CONTROLS
     ------------------------------------------------------------------------ */
  function setStage(stageName, silent = false) {
    state.stage = stageName;
    const viewport = stageRoot.querySelector('.stage-viewport');
    if (viewport) viewport.dataset.activeStage = stageName;
    stageRoot.querySelectorAll('.stage-panel').forEach((p) => {
      p.classList.toggle('active', p.dataset.stage === stageName);
    });
    syncQuickPadsUI();
    savePrefs();
    if (!silent) {
      const labels = { cover: 'COVER', vu: 'ANALOG VU', spectrum: 'SPECTRUM' };
      sfx('stage');
      showToast(`Top Stage: ${labels[stageName]}`);
    }
  }

  function cycleStage() {
    setStage(STAGES[(STAGES.indexOf(state.stage) + 1) % STAGES.length]);
  }

  function handleAuxNowJump() {
    closeAllSheets(false);
    if (state.tab === 'play' && !state.inSettings) {
      const trk = getCurrentTrack();
      if (trk) goToAlbum(trk.albumId);
      else sfx('bump');
    } else {
      setTab('play');
      showToast('Deck');
    }
  }

  function syncVfdIcons() {
    if (!window.DECK_VFD_ICONS) return;
    const isVfd = state.theme === 'vfd';
    window.DECK_VFD_ICONS.applyVfdIcons(consoleRoot, isVfd);
    window.DECK_VFD_ICONS.applyVfdIcons(stageRoot, isVfd);
  }

  // Keep every dynamically inserted or updated <use href="#ic-..."> in sync with VFD 16×16 dot-matrix sprite
  const vfdIconObserver = new MutationObserver(() => {
    if (state.theme === 'vfd') syncVfdIcons();
  });
  vfdIconObserver.observe(consoleRoot, { childList: true, subtree: true, attributes: true, attributeFilter: ['href'] });
  vfdIconObserver.observe(stageRoot, { childList: true, subtree: true, attributes: true, attributeFilter: ['href'] });

  function setTheme(theme, silent = false) {
    if (!silent) sfx('tick');
    state.theme = theme;
    document.documentElement.setAttribute('data-theme', theme);
    document.querySelectorAll('[data-set-theme]').forEach((b) => {
      b.classList.toggle('active', b.dataset.setTheme === theme);
    });
    syncVfdIcons();
    savePrefs();
    if (!state.currentTrackId) drawAlbumArt($s('#albumArtCanvas'), null);
    drawWaveform();
    syncTopTextFadeMasks();
    if (!silent) showToast(`Theme: ${theme.toUpperCase()}`);
  }

  function setView(view, silent = false) {
    if (!silent) sfx('tick');
    if (view === 'physical' && state.integerScale) {
      state.integerScale = false;
    }
    state.view = view;
    document.documentElement.setAttribute('data-view', view);
    document.querySelectorAll('[data-set-view]').forEach((b) => {
      b.classList.toggle('active', b.dataset.setView === view);
    });
    savePrefs();
    requestAnimationFrame(fitDeviceToViewport);
  }

  function toggleLid(forceVal) {
    state.lidClosed = typeof forceVal === 'boolean' ? forceVal : !state.lidClosed;
    sfx(state.lidClosed ? 'dim' : 'confirm');
    const overlay = document.getElementById('pocketLidOverlay');
    overlay.classList.toggle('active', state.lidClosed);
    overlay.setAttribute('aria-hidden', String(!state.lidClosed));
    document.getElementById('btnLidToggle').textContent = state.lidClosed ? '⌷ Open Lid' : '⌸ Close Lid';

    if (state.lidClosed && state.lidMode === 'pause' && audio.isPlaying) {
      audio.pause();
      updatePlayStateUI();
      showToast('Lid Closed — Paused');
    } else if (state.lidClosed) {
      showToast('Pocket DAP Mode — Displays Off');
    } else {
      showToast('Clamshell Opened');
    }
  }

  /* ------------------------------------------------------------------------
     CONTEXT MENU (EVOKED BY ⋯ OR Y KEY!), SORT SHEET & PLAYLIST PICKER
     All sheets support 100% Keypad Navigation (D-Pad + A + B)!
     ------------------------------------------------------------------------ */
  function renderTrackContextMenuButtons() {
    const ctx = state.actionCtx;
    if (!ctx) return;
    const trk = getTrackById(ctx.trackId);
    if (!trk) return;

    const container = $c('#actionSheetButtons');
    container.innerHTML = '';

    const actions = [];
    if (ctx.source === 'play') {
      // On PLAY: Shuffle & Repeat switches at the top — cycling them keeps the panel open!
      const repeatVal = state.repeat === 'one' ? 'One' : state.repeat === 'all' ? 'All' : 'Off';
      actions.push({
        icon: 'ic-shuffle',
        label: 'Shuffle',
        value: state.shuffle ? 'On' : 'Off',
        valActive: state.shuffle,
        run: () => {
          sfx('tick');
          state.shuffle = !state.shuffle;
          syncQuickPadsUI();
          savePrefs();
          showToast(state.shuffle ? 'Shuffle ON' : 'Shuffle OFF');
          renderTrackContextMenuButtons();
        },
      });
      actions.push({
        icon: state.repeat === 'one' ? 'ic-repeat-one' : 'ic-repeat',
        label: 'Repeat',
        value: repeatVal,
        valActive: state.repeat !== 'off',
        run: () => {
          sfx('tick');
          const order = ['all', 'one', 'off'];
          state.repeat = order[(order.indexOf(state.repeat) + 1) % order.length];
          syncQuickPadsUI();
          savePrefs();
          showToast(`Repeat: ${state.repeat.toUpperCase()}`);
          renderTrackContextMenuButtons();
        },
      });
    } else if (ctx.source === 'queue') {
      // Inside Queue: NO "Add to Queue"! Instead Play Next + Remove from Queue!
      actions.push({
        icon: 'ic-play-next',
        label: 'Play Next',
        run: () => { playNextInQueue(trk.id, ctx.queueIdx); closeAllSheets(false); },
      });
    } else {
      actions.push({
        icon: 'ic-play-next',
        label: 'Play Next',
        run: () => { playNextInQueue(trk.id, -1); closeAllSheets(false); },
      });
      actions.push({
        icon: 'ic-queue-add',
        label: 'Add to Queue',
        run: () => {
          state.queue.push(trk.id);
          sfx('add');
          syncTrackUI();
          renderQueue();
          showToast(`Added to Queue: ${trk.title}`);
          closeAllSheets(false);
        },
      });
    }

    actions.push({
      icon: 'ic-goto-album',
      label: 'Go to Album',
      run: () => goToAlbum(trk.albumId),
    });
    actions.push({
      icon: 'ic-artist',
      label: 'Go to Artist',
      run: () => goToArtist(trk.artist),
    });
    actions.push({
      icon: trk.starred ? 'ic-star-filled' : 'ic-star',
      label: trk.starred ? 'Unstar' : 'Star',
      run: () => { toggleStarTrack(trk.id); closeAllSheets(false); },
    });
    actions.push({
      icon: 'ic-playlist-add',
      label: 'Add to Playlist',
      run: () => openPlaylistPicker(trk.id),
    });
    actions.push({
      icon: 'ic-info',
      label: 'Get Info',
      run: () => openTrackInfoSheet(trk.id),
    });

    if (ctx.source === 'queue') {
      actions.push({
        icon: 'ic-trash',
        label: 'Remove from Queue',
        danger: true,
        run: () => { removeFromQueue(ctx.queueIdx); closeAllSheets(false); },
      });
    } else if (ctx.source === 'playlist' && ctx.playlistId && ctx.playlistId !== '__starred__') {
      actions.push({
        icon: 'ic-trash',
        label: 'Remove from Playlist',
        danger: true,
        run: () => {
          const pl = state.playlists.find((p) => p.id === ctx.playlistId);
          if (pl) {
            pl.trackIds = pl.trackIds.filter((id) => id !== trk.id);
            sfx('back');
            savePrefs();
            showToast(`Removed from ${pl.name}`);
            renderCatalog();
          }
          closeAllSheets(false);
        },
      });
    }

    const focusIdx = Math.max(0, Math.min(actions.length - 1, state.sheetFocusIdx));
    state.sheetFocusIdx = focusIdx;

    actions.forEach((act, idx) => {
      const btn = document.createElement('button');
      btn.className = `sheet-big-btn${act.active ? ' active-opt' : ''}${act.danger ? ' danger-btn' : ''}${idx === focusIdx ? ' kp-focus' : ''}`;
      const valHtml = act.value ? `<span class="sheet-btn-val${act.valActive ? ' is-on' : ''}">${act.value}</span>` : '';
      btn.innerHTML = `<span class="sheet-btn-ic"><svg class="ic-28"><use href="#${act.icon}"/></svg></span><span class="sheet-btn-lbl">${act.label}</span>${valHtml}`;
      btn.addEventListener('click', () => {
        state.sheetFocusIdx = idx;
        act.run();
      });
      container.appendChild(btn);
    });
  }

  function openTrackContextMenu(ctx) {
    if (!ctx || !ctx.trackId) { sfx('bump'); return; }
    const trk = getTrackById(ctx.trackId);
    if (!trk) return;
    sfx('tick');
    state.actionCtx = ctx;
    state.sheetFocusIdx = 0;

    $c('#actionTrackTitle').textContent = trk.title;
    $c('#actionTrackSub').textContent = `${trk.artist} — ${trk.album}`;

    renderTrackContextMenuButtons();
    $c('#trackActionSheet').hidden = false;
  }

  function openTrackInfoSheet(trackId) {
    const trk = getTrackById(trackId);
    if (!trk) return;
    sfx('tick');
    $c('#trackActionSheet').hidden = true;

    const alb = getAlbumById(trk.albumId);
    const discCount = alb ? alb.discCount : 1;
    const discTracks = alb ? alb.tracks.filter((t) => (t.discNo || 1) === (trk.discNo || 1)).length : 1;
    const totalAlbTracks = alb ? alb.tracks.length : 1;
    const kbps = parseInt(String(trk.bitrate || '2400').replace(/[^0-9]/g, ''), 10) || 2400;
    const fileSizeMb = ((kbps * 1000 / 8) * (trk.duration || 180) / (1024 * 1024)).toFixed(1);
    const ext = trk.format.split(' ')[0].toLowerCase();
    const safeName = trk.title.replace(/[:/\\?*"<>|]/g, '_');
    const discPrefix = discCount > 1 ? `Disc ${trk.discNo || 1}/` : '';
    const filePath = `/mnt/sd2/Music/${trk.artist}/${trk.year} - ${trk.album}/${discPrefix}${String(trk.trackNo || 1).padStart(2, '0')} - ${safeName}.${ext}`;
    const bitDepth = trk.format.includes('24') ? '24-bit' : trk.format.includes('16') ? '16-bit' : 'Lossy';
    const codecType = ext === 'mp3' ? 'Lossy' : 'Lossless';
    const rgTrack = (-4.2 - ((trk.duration || 180) % 45) / 10).toFixed(1);
    const rgAlbum = (parseFloat(rgTrack) + 0.6).toFixed(1);

    drawAlbumArt($c('#infoArtCanvas'), trk);
    $c('#infoTrackTitle').textContent = trk.title;
    $c('#infoTrackArtist').textContent = trk.artist;

    const body = $c('#infoScrollBody');
    body.scrollTop = 0;

    const rows = [
      { lbl: 'ALBUM', val: trk.album, full: true },
      { lbl: 'ARTIST', val: trk.artist },
      { lbl: 'YEAR', val: String(trk.year || '—'), mono: true },
      { lbl: 'TRACK / DISC', val: discCount > 1 ? `Disc ${trk.discNo || 1} of ${discCount}  (#${trk.trackNo || 1} of ${discTracks})` : `Track ${trk.trackNo || 1} of ${totalAlbTracks}`, mono: true },
      { lbl: 'DURATION', val: fmtTime(trk.duration), mono: true },
      { lbl: 'FORMAT', val: `${trk.format} (${codecType})`, mono: true },
      { lbl: 'SAMPLE RATE', val: `${trk.sampleRate}  ${bitDepth}`, mono: true },
      { lbl: 'BITRATE', val: trk.bitrate || '—', mono: true },
      { lbl: 'CHANNELS', val: 'Stereo (2.0)', mono: true },
      { lbl: 'FILE SIZE', val: `${fileSizeMb} MB`, mono: true },
      { lbl: 'ADDED DATE', val: trk.addedAt || '—', mono: true },
      { lbl: 'REPLAYGAIN', val: `${rgTrack} dB (Track) / ${rgAlbum} dB (Album)`, mono: true, full: true },
      { lbl: 'SIGNAL PATH', val: 'ALSA Direct (Bit-Perfect Hardware Output)', full: true },
      { lbl: 'FILE PATH', val: filePath, mono: true, full: true },
    ];

    const grid = document.createElement('div');
    grid.className = 'info-grid';
    rows.forEach((r) => {
      const cell = document.createElement('div');
      cell.className = `info-cell${r.full ? ' full-span' : ''}`;
      cell.innerHTML = `<span class="info-lbl">${r.lbl}</span><span class="info-val${r.mono ? ' mono' : ''}${r.accent ? ' accent' : ''}">${r.val}</span>`;
      grid.appendChild(cell);
    });
    body.innerHTML = '';
    body.appendChild(grid);

    $c('#trackInfoSheet').hidden = false;
  }

  function openPlaylistPicker(trackId) {
    sfx('tick');
    const trk = trackId ? getTrackById(trackId) : null;
    state.pickerTrackId = trk ? trk.id : null;
    state.sheetFocusIdx = 0;
    $c('#trackActionSheet').hidden = true;
    $c('#pickerTrackLabel').textContent = trk ? `${trk.title} — ${trk.artist}` : 'Create a new playlist';
    $c('#newPlaylistInput').value = '';
    renderPlaylistPickerList();
    $c('#playlistPickerSheet').hidden = false;
  }

  function renderPlaylistPickerList() {
    const list = $c('#pickerPlaylistList');
    list.innerHTML = '';
    const trk = state.pickerTrackId ? getTrackById(state.pickerTrackId) : null;
    if (!trk) return;

    const rows = [];
    const starRow = document.createElement('button');
    starRow.className = `picker-row${trk.starred ? ' member' : ''}`;
    starRow.innerHTML = `<span>★ Starred</span><span class="picker-check">${trk.starred ? '✓ Added' : '＋ Add'}</span>`;
    starRow.addEventListener('click', () => {
      toggleStarTrack(trk.id);
      renderPlaylistPickerList();
    });
    rows.push(starRow);
    list.appendChild(starRow);

    state.playlists.forEach((pl) => {
      const inPl = pl.trackIds.includes(trk.id);
      const row = document.createElement('button');
      row.className = `picker-row${inPl ? ' member' : ''}`;
      row.innerHTML = `<span>♫ ${pl.name} (${pl.trackIds.length})</span><span class="picker-check">${inPl ? '✓ Added' : '＋ Add'}</span>`;
      row.addEventListener('click', () => {
        if (inPl) {
          pl.trackIds = pl.trackIds.filter((id) => id !== trk.id);
          sfx('back');
          showToast(`Removed from ${pl.name}`);
        } else {
          pl.trackIds.push(trk.id);
          sfx('add');
          showToast(`Added to ${pl.name}`);
        }
        savePrefs();
        renderPlaylistPickerList();
        renderActiveView();
      });
      rows.push(row);
      list.appendChild(row);
    });

    const clampIdx = Math.max(0, Math.min(rows.length - 1, state.sheetFocusIdx));
    state.sheetFocusIdx = clampIdx;
    rows.forEach((r, i) => r.classList.toggle('kp-focus', i === clampIdx));
  }

  function getSortSheetAllButtons() {
    return [
      ...Array.from($c('#sortOptionsGrid').children),
      ...Array.from($c('#sortDirGrid').children),
    ];
  }

  function renderSortSheetOptions() {
    const opts = SORT_OPTIONS[state.tab];
    if (!opts) return;
    const cur = state.sortPrefs[state.tab];
    const activeMeta = opts.find((o) => o.field === cur.field) || opts[0];

    $c('#sortSheetSub').textContent = `${activeMeta.label} (${cur.dir === 'asc' ? activeMeta.asc.slice(2) : activeMeta.desc.slice(2)})`;

    const fieldGrid = $c('#sortOptionsGrid');
    const dirGrid = $c('#sortDirGrid');
    fieldGrid.innerHTML = '';
    dirGrid.innerHTML = '';

    // 1) ORDER BY Fields (Added Time first!)
    opts.forEach((opt) => {
      const isCur = cur.field === opt.field;
      const btn = document.createElement('button');
      btn.className = `sheet-big-btn${isCur ? ' active-opt' : ''}`;
      const modeHint = opt.mode === 'alpha' ? 'A–Z' : opt.mode === 'year' ? 'Year' : '#';
      btn.innerHTML = `<span>${opt.label}</span><span style="opacity:.72;font-family:var(--font-mono);font-size:var(--t-cap);">${modeHint}</span>`;
      btn.addEventListener('click', () => {
        cur.field = opt.field;
        if (opt.field === 'added' || opt.field === 'year' || opt.field === 'albums' || opt.field === 'tracks') {
          cur.dir = 'desc';
        } else if (!isCur) {
          cur.dir = 'asc';
        }
        sfx('confirm');
        savePrefs();
        state.listFocusIdx = 0;
        $c('#libItems').scrollTop = 0;
        renderCatalog();
        renderSortSheetOptions();
        showToast(`${opt.label}: ${cur.dir === 'asc' ? opt.asc : opt.desc}`);
      });
      fieldGrid.appendChild(btn);
    });

    // 2) Context-Clear DIRECTION buttons (e.g. "↓ Newest First / ↑ Oldest First" or "↑ A → Z / ↓ Z → A")
    const dirOrder = activeMeta.mode === 'alpha'
      ? [{ dir: 'asc', label: activeMeta.asc }, { dir: 'desc', label: activeMeta.desc }]
      : [{ dir: 'desc', label: activeMeta.desc }, { dir: 'asc', label: activeMeta.asc }];

    dirOrder.forEach((d) => {
      const btn = document.createElement('button');
      btn.className = `sheet-big-btn dir-opt${cur.dir === d.dir ? ' active-opt' : ''}`;
      btn.textContent = d.label;
      btn.addEventListener('click', () => {
        cur.dir = d.dir;
        sfx('confirm');
        savePrefs();
        state.listFocusIdx = 0;
        $c('#libItems').scrollTop = 0;
        renderCatalog();
        renderSortSheetOptions();
        showToast(`${activeMeta.label}: ${d.label}`);
      });
      dirGrid.appendChild(btn);
    });

    const allBtns = getSortSheetAllButtons();
    const clampIdx = Math.max(0, Math.min(allBtns.length - 1, state.sheetFocusIdx));
    state.sheetFocusIdx = clampIdx;
    allBtns.forEach((el, i) => el.classList.toggle('kp-focus', i === clampIdx));
  }

  function openSortOrderSheet() {
    const opts = SORT_OPTIONS[state.tab];
    if (!opts) return;
    sfx('tick');
    const cur = state.sortPrefs[state.tab];
    state.sheetFocusIdx = Math.max(0, opts.findIndex((o) => o.field === cur.field));
    $c('#sortSheetTitle').textContent = `Order ${state.tab === 'tracks' ? 'Songs' : state.tab[0].toUpperCase() + state.tab.slice(1)}`;
    renderSortSheetOptions();
    $c('#sortOrderSheet').hidden = false;
  }

  function activeSheetType() {
    if (!$c('#trackActionSheet').hidden) return 'action';
    if (!$c('#sortOrderSheet').hidden) return 'sort';
    if (!$c('#playlistPickerSheet').hidden) return 'picker';
    if (!$c('#outputDeviceSheet').hidden) return 'output';
    if (!$c('#trackInfoSheet').hidden) return 'info';
    return null;
  }

  function closeAllSheets(playBackSfx = false) {
    const wasOpen = activeSheetType() !== null;
    closeOsk(false);
    $c('#trackActionSheet').hidden = true;
    $c('#playlistPickerSheet').hidden = true;
    $c('#sortOrderSheet').hidden = true;
    $c('#outputDeviceSheet').hidden = true;
    $c('#trackInfoSheet').hidden = true;
    state.actionCtx = null;
    state.pickerTrackId = null;
    if (playBackSfx && wasOpen) sfx('back');
  }

  /* ------------------------------------------------------------------------
     PLAY & SETTINGS KEYPAD FOCUS VISUAL SYNC
     ------------------------------------------------------------------------ */
  function syncPlayFocusUI() {
    // Play deck uses direct hardware mappings (no visual cursor box!)
    $$c('[data-pf]').forEach((el) => el.classList.remove('kp-focus'));
  }

  function syncSettingsFocusUI() {
    $$c('[data-sf]').forEach((el) => {
      el.classList.toggle('kp-focus', Number(el.dataset.sf) === state.settingsFocusRow);
    });
  }

  /* ------------------------------------------------------------------------
     DECK 01: WAVEFORM SCRUBBER
     ------------------------------------------------------------------------ */
  const wfCanvas = $c('#waveformCanvas');
  const wfCtx = wfCanvas.getContext('2d');

  function drawWaveform() {
    const trk = getCurrentTrack();
    const tc = getThemeColors();
    const w = wfCanvas.width;
    const h = wfCanvas.height;

    wfCtx.clearRect(0, 0, w, h);

    if (!trk) {
      if (state.theme === 'vfd') {
        const cell = 8;
        const dot = 6;
        const cols = Math.floor(w / cell);
        const rows = Math.floor(h / cell);
        for (let c = 0; c < cols; c++) {
          for (let r = 0; r < rows; r++) {
            wfCtx.fillStyle = '#07171c';
            wfCtx.fillRect(c * cell + 1, r * cell + 1, dot, dot);
          }
        }
      } else {
        wfCtx.fillStyle = tc.border;
        wfCtx.fillRect(0, Math.floor(h / 2) - 1, w, 2);
      }
      return;
    }

    const raw = trk.waveform || generateWaveform(1);
    const progress = audio.duration > 0 ? audio.currentTime / audio.duration : 0;

    if (state.theme === 'vfd') {
      // Authentic Dot-Matrix VFD Waveform: discrete 6×6 phosphor cells on an 8×8 grid!
      const cell = 8;
      const dot = 6;
      const cols = Math.floor(w / cell);
      const rows = Math.floor(h / cell);
      const midRow = (rows - 1) / 2;
      const playCol = Math.floor(progress * cols);

      for (let c = 0; c < cols; c++) {
        const frac = c / cols;
        const amp = raw[Math.floor(frac * raw.length)] || 0.4;
        const halfSpan = Math.max(1, Math.round(amp * (rows * 0.46)));
        const isPast = c <= playCol;
        const isCursor = c === playCol;

        for (let r = 0; r < rows; r++) {
          const dist = Math.abs(r - midRow);
          const inWave = dist <= halfSpan;
          if (isCursor) {
            wfCtx.fillStyle = tc.danger;
          } else if (inWave) {
            wfCtx.fillStyle = isPast ? (dist > rows * 0.34 ? tc.danger : tc.accent) : '#124652';
          } else {
            wfCtx.fillStyle = '#07171c';
          }
          wfCtx.fillRect(c * cell + 1, r * cell + 1, dot, dot);
        }
      }
      return;
    }

    const barCount = 80;
    const step = w / barCount;
    const barW = Math.max(5, step - 4);

    for (let i = 0; i < barCount; i++) {
      const frac = i / barCount;
      const amp = raw[Math.floor(frac * raw.length)] || 0.4;
      const bh = Math.max(12, amp * (h - 20));
      const y = (h - bh) / 2;
      wfCtx.fillStyle = frac <= progress ? tc.accent : tc.wfInactive;
      wfCtx.beginPath();
      wfCtx.roundRect(i * step + 2, y, barW, bh, 3);
      wfCtx.fill();
    }

    const px = progress * w;
    wfCtx.fillStyle = tc.text;
    wfCtx.fillRect(px - 2.5, 4, 5, h - 8);
  }

  let scrubbingWf = false;
  function handleWfPointer(e) {
    if (!state.currentTrackId || audio.duration <= 0) return;
    const rect = wfCanvas.getBoundingClientRect();
    const norm = Math.max(0, Math.min(1, (e.clientX - rect.left) / rect.width));
    audio.seek(norm * audio.duration);
    drawWaveform();
  }
  wfCanvas.addEventListener('pointerdown', (e) => {
    scrubbingWf = true;
    wfCanvas.setPointerCapture(e.pointerId);
    handleWfPointer(e);
  });
  wfCanvas.addEventListener('pointermove', (e) => { if (scrubbingWf) handleWfPointer(e); });
  wfCanvas.addEventListener('pointerup', () => { scrubbingWf = false; });

  /* ------------------------------------------------------------------------
     SMART CONTEXT-AWARE RIGHT SCRUBBER + VIRTUALIZED RENDERER
     ------------------------------------------------------------------------ */
  const libItemsEl = $c('#libItems');
  const smartScrubberEl = $c('#smartScrubber');
  const scrubberTicksEl = $c('#scrubberTicks');
  const scrubberThumbEl = $c('#scrubberThumb');
  const scrubberPopupEl = $c('#scrubberPopup');
  const scrubberPopupHeroEl = $c('#scrubberPopupHero');
  const scrubberPopupSubEl = $c('#scrubberPopupSub');

  const queueListEl = $c('#queueList');
  const queueScrubberEl = $c('#queueScrubber');
  const queueScrubberTicksEl = $c('#queueScrubberTicks');
  const queueScrubberThumbEl = $c('#queueScrubberThumb');
  const queueScrubberPopupEl = $c('#queueScrubberPopup');
  const queueScrubberPopupHeroEl = $c('#queueScrubberPopupHero');
  const queueScrubberPopupSubEl = $c('#queueScrubberPopupSub');

  let activeVirtualRender = null;
  let lastScrubHero = '';
  let lastScrubSfxTs = 0;
  let popupHideTimer = null;
  let queuePopupHideTimer = null;
  let lastQueueScrubHero = '';

  function setThumbTransform(thumbEl, px) {
    thumbEl.style.transform = `translate3d(0, ${px.toFixed(2)}px, 0)`;
  }

  function syncScrollbarVisual(scrollEl, railEl, thumbEl, knownTotalH = 0) {
    if (!scrollEl || !railEl || !thumbEl || railEl.hidden) return;
    const viewH = scrollEl.clientHeight || 1;
    const contentH = knownTotalH > 0 ? Math.max(knownTotalH, scrollEl.scrollHeight) : scrollEl.scrollHeight;
    const maxScroll = Math.max(0, contentH - viewH);
    const trackH = Math.max(1, railEl.clientHeight - 20 - 44);
    thumbEl.style.opacity = maxScroll > 0 ? '1' : '0.35';
    const ratio = maxScroll > 0 ? Math.max(0, Math.min(1, scrollEl.scrollTop / maxScroll)) : 0;
    setThumbTransform(thumbEl, ratio * trackH);
  }

  function renderNumericTicks(ticksEl, n) {
    ticksEl.innerHTML = '';
    if (n <= 0) return;
    const marks = n >= 10
      ? ['1', Math.round(n * 0.25), Math.round(n * 0.5), Math.round(n * 0.75), String(n)]
      : n > 1
        ? ['1', String(n)]
        : ['1'];
    marks.forEach((m) => {
      const sp = document.createElement('span');
      sp.textContent = Number(m) >= 1000 ? `${(Number(m) / 1000).toFixed(1)}k` : String(m);
      ticksEl.appendChild(sp);
    });
  }

  function configureSmartScrubber(category, items, sortMeta, totalHeight) {
    smartScrubberEl.hidden = false;
    state.activeScrubContext = { category, items, sortMeta, totalHeight };

    scrubberTicksEl.innerHTML = '';
    if (!items.length) {
      requestAnimationFrame(syncScrubberThumbFromScroll);
      return;
    }
    if (sortMeta.mode === 'alpha') {
      const letters = sortMeta.dir === 'desc'
        ? ['Z', 'V', 'S', 'P', 'M', 'J', 'G', 'D', 'A']
        : ['A', 'D', 'G', 'J', 'M', 'P', 'S', 'V', 'Z'];
      letters.forEach((ch) => {
        const sp = document.createElement('span');
        sp.textContent = ch;
        scrubberTicksEl.appendChild(sp);
      });
    } else if (sortMeta.mode === 'year') {
      const years = [items[0].year, items[Math.floor(items.length * 0.25)].year, items[Math.floor(items.length * 0.5)].year, items[Math.floor(items.length * 0.75)].year, items[items.length - 1].year];
      years.forEach((yr, idx) => {
        const sp = document.createElement('span');
        sp.textContent = idx === 0 || idx === 4 ? String(yr || '') : String(yr || '').slice(-2);
        scrubberTicksEl.appendChild(sp);
      });
    } else {
      renderNumericTicks(scrubberTicksEl, items.length);
    }
    syncScrubberThumbFromScroll();
    requestAnimationFrame(syncScrubberThumbFromScroll);
  }

  function syncScrubberThumbFromScroll() {
    const knownH = (state.activeScrubContext && state.activeScrubContext.sortMeta.mode !== 'scroll')
      ? (state.activeScrubContext.totalHeight || 0)
      : 0;
    syncScrollbarVisual(libItemsEl, smartScrubberEl, scrubberThumbEl, knownH);
  }

  function syncQueueScrubberThumb() {
    syncScrollbarVisual(queueListEl, queueScrubberEl, queueScrubberThumbEl, 0);
  }

  function configureQueueScrubber() {
    renderNumericTicks(queueScrubberTicksEl, state.queue.length);
    syncQueueScrubberThumb();
    requestAnimationFrame(syncQueueScrubberThumb);
  }

  const scrollAnims = new WeakMap();
  let lastCatalogRenderKey = '';

  function cancelSmoothScroll(el) {
    const rafId = scrollAnims.get(el);
    if (rafId) {
      cancelAnimationFrame(rafId);
      scrollAnims.delete(el);
    }
  }

  function smoothScrollTo(el, targetTop, durationMs = 200) {
    if (!el) return;
    cancelSmoothScroll(el);
    const maxScroll = Math.max(0, el.scrollHeight - el.clientHeight);
    const dest = Math.max(0, Math.min(maxScroll, targetTop));
    const start = el.scrollTop;
    const delta = dest - start;
    if (Math.abs(delta) < 1 || durationMs <= 0) {
      el.scrollTop = dest;
      if (el === libItemsEl) {
        if (activeVirtualRender) activeVirtualRender(true);
        syncScrubberThumbFromScroll();
      } else if (el === queueListEl) {
        syncQueueScrubberThumb();
      }
      return;
    }
    const t0 = performance.now();
    const step = (now) => {
      const p = Math.min(1, (now - t0) / durationMs);
      const eased = 1 - Math.pow(1 - p, 3);
      el.scrollTop = start + delta * eased;
      if (el === libItemsEl) {
        if (activeVirtualRender) activeVirtualRender(false);
        syncScrubberThumbFromScroll();
      } else if (el === queueListEl) {
        syncQueueScrubberThumb();
      }
      if (p < 1) {
        scrollAnims.set(el, requestAnimationFrame(step));
      } else {
        scrollAnims.delete(el);
        el.scrollTop = dest;
        if (el === libItemsEl) {
          if (activeVirtualRender) activeVirtualRender(true);
          syncScrubberThumbFromScroll();
        } else if (el === queueListEl) {
          syncQueueScrubberThumb();
        }
      }
    };
    scrollAnims.set(el, requestAnimationFrame(step));
  }

  function getOffsetTopWithin(el, ancestor) {
    let top = 0;
    let cur = el;
    while (cur && cur !== ancestor) {
      top += cur.offsetTop;
      cur = cur.offsetParent;
    }
    return top;
  }

  function smoothScrollIntoView(container, targetEl, durationMs = 170) {
    if (!container || !targetEl) return;
    const relTop = getOffsetTopWithin(targetEl, container);
    const relBottom = relTop + targetEl.offsetHeight;
    const viewH = container.clientHeight;
    const pad = 8;
    let dest = container.scrollTop;
    if (relTop - pad < container.scrollTop) {
      dest = Math.max(0, relTop - pad);
    } else if (relBottom + pad > container.scrollTop + viewH) {
      dest = relBottom + pad - viewH;
    } else {
      return;
    }
    smoothScrollTo(container, dest, durationMs);
  }

  function getAlbumsLayout() {
    return state.albumView === 'list'
      ? { cols: 1, rowH: 130 }
      : { cols: 3, rowH: 382 };
  }

  function scrollToFocusedCatalogItem(isPageJump = false) {
    const viewH = libItemsEl.clientHeight || 560;
    const albLayout = getAlbumsLayout();
    const rowH = state.tab === 'albums' ? albLayout.rowH : state.tab === 'tracks' ? 128 : 108;
    const cols = state.tab === 'albums' ? albLayout.cols : 1;
    const rowIdx = Math.floor(state.listFocusIdx / cols);
    const top = rowIdx * rowH;
    const bottom = top + rowH;
    let dest = libItemsEl.scrollTop;
    if (isPageJump) {
      dest = top < libItemsEl.scrollTop ? Math.max(0, top - 4) : Math.max(0, top - 4);
    } else if (top < libItemsEl.scrollTop) {
      dest = top;
    } else if (bottom > libItemsEl.scrollTop + viewH) {
      dest = bottom - viewH;
    }
    if (activeVirtualRender) activeVirtualRender(true);
    if (Math.abs(dest - libItemsEl.scrollTop) >= 1) {
      smoothScrollTo(libItemsEl, dest, isPageJump ? 250 : 155);
    } else {
      syncScrubberThumbFromScroll();
    }
  }

  function handleSmartScrub(clientY) {
    const ctx = state.activeScrubContext;
    if (!ctx || !ctx.items.length) return;
    const rect = smartScrubberEl.getBoundingClientRect();
    const norm = Math.max(0, Math.min(1, (clientY - rect.top) / rect.height));
    const items = ctx.items;
    const n = items.length;

    let targetIdx = Math.min(n - 1, Math.floor(norm * n));
    let heroText = '';
    let subText = '';

    if (ctx.sortMeta.mode === 'alpha') {
      const alphaStr = ctx.sortMeta.dir === 'desc' ? 'ZYXWVUTSRQPONMLKJIHGFEDCBA' : 'ABCDEFGHIJKLMNOPQRSTUVWXYZ';
      const targetChar = alphaStr[Math.min(25, Math.floor(norm * 26))];
      const keyFn = (it) => {
        if (ctx.category === 'albums') return (ctx.sortMeta.field === 'artist' ? it.artist : it.title).toUpperCase();
        if (ctx.category === 'tracks') return (ctx.sortMeta.field === 'artist' ? it.artist : ctx.sortMeta.field === 'album' ? it.album : it.title).toUpperCase();
        return it.name.toUpperCase();
      };
      const found = items.findIndex((it) => ctx.sortMeta.dir === 'desc' ? keyFn(it) <= targetChar : keyFn(it) >= targetChar);
      targetIdx = found >= 0 ? found : n - 1;
      const item = items[targetIdx];
      heroText = targetChar;
      subText = `${item.title || item.name} (#${targetIdx + 1})`;
    } else if (ctx.sortMeta.mode === 'year') {
      const item = items[targetIdx];
      heroText = String(item.year || '—');
      subText = `#${targetIdx + 1} / ${n} — ${item.title || item.name}`;
    } else if (ctx.sortMeta.mode === 'scroll') {
      const item = items[targetIdx];
      heroText = String(targetIdx + 1);
      subText = item ? `${item.title || item.name || ''}` : '';
    } else {
      const item = items[targetIdx];
      heroText = String(targetIdx + 1);
      if (ctx.sortMeta.field === 'added') {
        subText = `${item.addedAt || ''} — ${item.title || item.name}`;
      } else {
        const countStr = ctx.sortMeta.field === 'albums' ? `${item.albums.length} albums` : `${item.tracks.length} songs`;
        subText = `${item.name} (${countStr})`;
      }
    }

    cancelSmoothScroll(libItemsEl);
    const maxScroll = Math.max(0, libItemsEl.scrollHeight - libItemsEl.clientHeight);
    const trackH = Math.max(1, smartScrubberEl.clientHeight - 20 - 44);
    scrubberThumbEl.style.opacity = maxScroll > 0 ? '1' : '0.35';
    setThumbTransform(scrubberThumbEl, maxScroll > 0 ? norm * trackH : 0);

    if (ctx.sortMeta.mode === 'scroll') {
      libItemsEl.scrollTop = norm * maxScroll;
    } else {
      state.listFocusIdx = targetIdx;
      if (ctx.category === 'albums') {
        const { cols, rowH } = getAlbumsLayout();
        libItemsEl.scrollTop = Math.min(maxScroll, Math.floor(targetIdx / cols) * rowH);
      } else {
        const rowH = ctx.category === 'tracks' ? 128 : 108;
        libItemsEl.scrollTop = Math.min(maxScroll, targetIdx * rowH);
      }
      if (activeVirtualRender) activeVirtualRender(true);
    }

    scrubberPopupHeroEl.textContent = heroText;
    scrubberPopupSubEl.textContent = subText;
    scrubberPopupEl.hidden = false;
    clearTimeout(popupHideTimer);

    const now = performance.now();
    if (heroText !== lastScrubHero && now - lastScrubSfxTs > 45) {
      lastScrubHero = heroText;
      lastScrubSfxTs = now;
      sfx('tick');
    }
  }

  function handleQueueScrub(clientY) {
    const n = state.queue.length;
    if (!n) return;
    const rect = queueScrubberEl.getBoundingClientRect();
    const norm = Math.max(0, Math.min(1, (clientY - rect.top) / rect.height));
    const maxScroll = Math.max(0, queueListEl.scrollHeight - queueListEl.clientHeight);
    cancelSmoothScroll(queueListEl);
    const trackH = Math.max(1, queueScrubberEl.clientHeight - 20 - 44);
    queueScrubberThumbEl.style.opacity = maxScroll > 0 ? '1' : '0.35';
    setThumbTransform(queueScrubberThumbEl, maxScroll > 0 ? norm * trackH : 0);
    queueListEl.scrollTop = norm * maxScroll;

    const targetIdx = Math.min(n - 1, Math.floor(norm * n));
    const trk = getTrackById(state.queue[targetIdx]);
    const heroText = String(targetIdx + 1);
    queueScrubberPopupHeroEl.textContent = heroText;
    queueScrubberPopupSubEl.textContent = trk ? trk.title : '';
    queueScrubberPopupEl.hidden = false;
    clearTimeout(queuePopupHideTimer);

    const now = performance.now();
    if (heroText !== lastQueueScrubHero && now - lastScrubSfxTs > 45) {
      lastQueueScrubHero = heroText;
      lastScrubSfxTs = now;
      sfx('tick');
    }
  }

  let scrubbingQueueRail = false;
  queueScrubberEl.addEventListener('pointerdown', (e) => {
    scrubbingQueueRail = true;
    queueScrubberEl.classList.add('dragging');
    queueScrubberEl.setPointerCapture(e.pointerId);
    handleQueueScrub(e.clientY);
  });
  queueScrubberEl.addEventListener('pointermove', (e) => {
    if (scrubbingQueueRail) handleQueueScrub(e.clientY);
  });
  const endQueueRailScrub = () => {
    if (!scrubbingQueueRail) return;
    scrubbingQueueRail = false;
    queueScrubberEl.classList.remove('dragging');
    queuePopupHideTimer = setTimeout(() => { queueScrubberPopupEl.hidden = true; }, 220);
  };
  queueScrubberEl.addEventListener('pointerup', endQueueRailScrub);
  queueScrubberEl.addEventListener('pointercancel', endQueueRailScrub);
  queueListEl.addEventListener('scroll', () => {
    if (!scrubbingQueueRail) syncQueueScrubberThumb();
  }, { passive: true });

  let scrubbingRail = false;
  smartScrubberEl.addEventListener('pointerdown', (e) => {
    scrubbingRail = true;
    smartScrubberEl.classList.add('dragging');
    smartScrubberEl.setPointerCapture(e.pointerId);
    handleSmartScrub(e.clientY);
  });
  smartScrubberEl.addEventListener('pointermove', (e) => {
    if (scrubbingRail) handleSmartScrub(e.clientY);
  });
  const endRailScrub = () => {
    if (!scrubbingRail) return;
    scrubbingRail = false;
    smartScrubberEl.classList.remove('dragging');
    popupHideTimer = setTimeout(() => { scrubberPopupEl.hidden = true; }, 220);
  };
  smartScrubberEl.addEventListener('pointerup', endRailScrub);
  smartScrubberEl.addEventListener('pointercancel', endRailScrub);

  function isSearchBarActive() {
    return state.searchOpen || state.searchQuery.trim().length > 0 || (state.oskOpen && state.oskTargetId === 'catSearchInput');
  }

  function getMinusOneControls() {
    if (isSearchBarActive()) {
      return [$c('#catSearchBar'), $c('#btnCatSearchClose')];
    }
    if (state.tab === 'albums') {
      return [
        $c('#catSearchBar'),
        $c('#btnCatShufflePlay'),
        $c('#btnCatPlayAll'),
        $c('#btnAlbumViewGrid'),
        $c('#btnAlbumViewList'),
        $c('#btnOpenSortSheet'),
      ];
    }
    if (state.tab === 'playlists') return [$c('#catSearchBar'), $c('#btnCatNewPlaylist')];
    return [$c('#catSearchBar'), $c('#btnCatShufflePlay'), $c('#btnCatPlayAll'), $c('#btnOpenSortSheet')];
  }

  function getDefaultMinusOneFocusIdx() {
    const controls = getMinusOneControls();
    const playIdx = controls.indexOf($c('#btnCatPlayAll'));
    return playIdx >= 0 ? playIdx : 0;
  }

  /* ------------------------------------------------------------------------
     ON-SCREEN TOUCH + D-PAD CURSOR VIRTUAL KEYPAD (DS / SWITCH STYLE)
     ------------------------------------------------------------------------ */
  const OSK_ROWS = {
    abc: [
      ['Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P'].map((ch) => ({ label: ch, val: ch.toLowerCase(), span: 1 })),
      [...'ASDFGHJKL'.split('').map((ch) => ({ label: ch, val: ch.toLowerCase(), span: 1 })), { label: '⌫', act: 'bksp', fn: true, span: 1 }],
      [...'ZXCVBNM-.\''.split('').map((ch) => ({ label: ch, val: ch.toLowerCase(), span: 1 }))],
      [
        { label: '123', act: 'mode', fn: true, span: 2 },
        { label: 'SPACE', act: 'space', fn: true, span: 4 },
        { label: 'CLR', act: 'clear', fn: true, span: 2 },
        { label: 'DONE', act: 'done', fn: true, done: true, span: 2 },
      ],
    ],
    num: [
      ['1', '2', '3', '4', '5', '6', '7', '8', '9', '0'].map((ch) => ({ label: ch, val: ch, span: 1 })),
      [...'-/:;()&@"'.split('').map((ch) => ({ label: ch, val: ch, span: 1 })), { label: '⌫', act: 'bksp', fn: true, span: 1 }],
      [...'+=!?_.,\'#~'.split('').map((ch) => ({ label: ch, val: ch, span: 1 }))],
      [
        { label: 'ABC', act: 'mode', fn: true, span: 2 },
        { label: 'SPACE', act: 'space', fn: true, span: 4 },
        { label: 'CLR', act: 'clear', fn: true, span: 2 },
        { label: 'DONE', act: 'done', fn: true, done: true, span: 2 },
      ],
    ],
  };

  function getOskRowKeys(rowIdx) {
    return OSK_ROWS[state.oskMode][rowIdx] || OSK_ROWS.abc[0];
  }

  function oskKeyIndexForLogicalCol(rowIdx, logicalCol) {
    const keys = getOskRowKeys(rowIdx);
    let colAcc = 0;
    for (let i = 0; i < keys.length; i++) {
      colAcc += keys[i].span;
      if (logicalCol < colAcc) return i;
    }
    return keys.length - 1;
  }

  function oskLogicalColForKey(rowIdx, keyIdx) {
    const keys = getOskRowKeys(rowIdx);
    let colAcc = 0;
    for (let i = 0; i < keyIdx && i < keys.length; i++) colAcc += keys[i].span;
    return colAcc + Math.floor((keys[keyIdx]?.span || 1) / 2);
  }

  function applyOskInputValue(newVal) {
    const input = state.oskTargetId ? $c(`#${state.oskTargetId}`) : null;
    if (!input) return;
    const maxLen = input.maxLength > 0 ? input.maxLength : 64;
    const clipped = newVal.slice(0, maxLen);
    input.value = clipped;
    if (state.oskTargetId === 'catSearchInput') {
      state.searchQuery = clipped;
      state.listFocusIdx = 0;
      libItemsEl.scrollTop = 0;
      syncMinusOneBarUI();
      renderCatalog();
    }
  }

  function triggerOskKey(keyDef) {
    const input = state.oskTargetId ? $c(`#${state.oskTargetId}`) : null;
    if (!input) return;
    const cur = input.value || '';
    if (keyDef.act === 'bksp') {
      if (cur.length > 0) {
        sfx('back');
        applyOskInputValue(cur.slice(0, -1));
      } else {
        sfx('bump');
      }
    } else if (keyDef.act === 'space') {
      sfx('tick');
      applyOskInputValue(`${cur} `);
    } else if (keyDef.act === 'clear') {
      if (cur.length > 0) {
        sfx('back');
        applyOskInputValue('');
      } else {
        sfx('bump');
      }
    } else if (keyDef.act === 'mode') {
      sfx('tick');
      state.oskMode = state.oskMode === 'abc' ? 'num' : 'abc';
      renderOsk();
    } else if (keyDef.act === 'done') {
      sfx('confirm');
      const targetId = state.oskTargetId;
      closeOsk(false);
      if (targetId === 'catSearchInput') {
        if (!state.searchQuery.trim()) state.searchOpen = false;
        state.inMinusOne = false;
        syncMinusOneBarUI();
        if (activeVirtualRender) activeVirtualRender(true);
        else if (state.tab === 'playlists') renderCatalog();
      } else if (targetId === 'newPlaylistInput' && cur.trim()) {
        $c('#btnCreateAndAddPl').click();
      }
    } else if (keyDef.val !== undefined) {
      sfx('tick');
      applyOskInputValue(cur + keyDef.val);
    }
  }

  function renderOsk() {
    const grid = $c('#dsOskGrid');
    grid.innerHTML = '';
    const rows = OSK_ROWS[state.oskMode];
    const activeKeyIdx = oskKeyIndexForLogicalCol(state.oskRow, state.oskCol);

    rows.forEach((rowKeys, rIdx) => {
      rowKeys.forEach((k, kIdx) => {
        const btn = document.createElement('button');
        const isFocused = rIdx === state.oskRow && kIdx === activeKeyIdx;
        btn.type = 'button';
        btn.className = `osk-key${k.fn ? ' fn-key' : ''}${k.done ? ' done-key' : ''}${k.span > 1 ? ` span-${k.span}` : ''}${isFocused ? ' kp-focus' : ''}`;
        btn.textContent = k.label;
        btn.addEventListener('pointerdown', (ev) => {
          // Keep physical keyboard input focus intact when tapping touch keys
          ev.preventDefault();
        });
        btn.addEventListener('click', () => {
          state.oskRow = rIdx;
          state.oskCol = oskLogicalColForKey(rIdx, kIdx);
          triggerOskKey(k);
          if (state.oskOpen) renderOsk();
        });
        grid.appendChild(btn);
      });
    });
  }

  function openOsk(targetId, resetPos = true) {
    state.oskOpen = true;
    state.oskTargetId = targetId;
    if (resetPos) {
      state.oskMode = 'abc';
      state.oskRow = 0;
      state.oskCol = 0;
    }
    $c('#dsOsk').hidden = false;
    $c('#catalogPanel').classList.toggle('osk-open', targetId === 'catSearchInput');
    renderOsk();
  }

  function closeOsk(playSfx = false) {
    if (!state.oskOpen) return;
    if (playSfx) sfx('back');
    state.oskOpen = false;
    const prevTarget = state.oskTargetId;
    state.oskTargetId = null;
    $c('#dsOsk').hidden = true;
    $c('#catalogPanel').classList.remove('osk-open');
    if (prevTarget) {
      const inp = $c(`#${prevTarget}`);
      if (inp) inp.blur();
    }
  }

  function handleOskKeypad(btn) {
    const rows = OSK_ROWS[state.oskMode];
    const rowKeys = rows[state.oskRow];
    const keyIdx = oskKeyIndexForLogicalCol(state.oskRow, state.oskCol);

    if (btn === 'B') {
      const targetId = state.oskTargetId;
      closeOsk(true);
      if (targetId === 'catSearchInput') {
        if (!state.searchQuery.trim()) state.searchOpen = false;
        state.inMinusOne = false;
        syncMinusOneBarUI();
        if (activeVirtualRender) activeVirtualRender(true);
        else if (state.tab === 'playlists') renderCatalog();
      }
      return;
    }
    if (btn === 'Y') {
      triggerOskKey({ act: 'bksp' });
      return;
    }
    if (btn === 'A') {
      triggerOskKey(rowKeys[keyIdx]);
      if (state.oskOpen) renderOsk();
      return;
    }
    if (btn === 'LEFT') {
      if (keyIdx > 0) {
        state.oskCol = oskLogicalColForKey(state.oskRow, keyIdx - 1);
        sfx('tick');
        renderOsk();
      } else sfx('bump');
      return;
    }
    if (btn === 'RIGHT') {
      if (keyIdx < rowKeys.length - 1) {
        state.oskCol = oskLogicalColForKey(state.oskRow, keyIdx + 1);
        sfx('tick');
        renderOsk();
      } else sfx('bump');
      return;
    }
    if (btn === 'UP') {
      if (state.oskRow > 0) {
        state.oskRow--;
        sfx('tick');
        renderOsk();
      } else sfx('bump');
      return;
    }
    if (btn === 'DOWN') {
      if (state.oskRow < rows.length - 1) {
        state.oskRow++;
        sfx('tick');
        renderOsk();
      } else sfx('bump');
      return;
    }
  }

  function openCatalogSearch() {
    if (state.detailStack.length || !isCatalogTab(state.tab)) return;
    sfx('tick');
    state.minusOneOpen = true;
    state.inMinusOne = true;
    state.searchOpen = true;
    state.minusOneFocusIdx = 0;
    const input = $c('#catSearchInput');
    input.value = state.searchQuery;
    syncMinusOneBarUI();
    openOsk('catSearchInput', true);
    requestAnimationFrame(() => input.focus());
  }

  function closeCatalogSearch(playSfx = true) {
    if (!state.searchOpen && !state.searchQuery) return;
    closeOsk(false);
    if (playSfx) sfx('back');
    state.searchOpen = false;
    state.searchQuery = '';
    state.inMinusOne = false;
    const input = $c('#catSearchInput');
    input.value = '';
    input.blur();
    state.listFocusIdx = 0;
    libItemsEl.scrollTop = 0;
    syncMinusOneBarUI();
    renderCatalog();
  }

  function getCatalogRootTrackIds() {
    const items = state.activeSortedItems;
    if (!items.length) return [];
    let ids = [];
    if (state.tab === 'tracks') {
      ids = items.map((t) => t.id);
    } else if (state.tab === 'albums') {
      for (let i = 0; i < items.length && ids.length < 500; i++) {
        for (const t of items[i].tracks) {
          ids.push(t.id);
          if (ids.length >= 500) break;
        }
      }
    } else if (state.tab === 'artists') {
      for (let i = 0; i < items.length && ids.length < 500; i++) {
        for (const t of items[i].tracks) {
          ids.push(t.id);
          if (ids.length >= 500) break;
        }
      }
    }
    return ids;
  }

  function playCatalogRoot(shuffle) {
    const ids = getCatalogRootTrackIds();
    if (!ids.length) return;
    const list = shuffle ? shuffleTrackIds(ids) : ids;
    const labelMap = { albums: 'Albums', artists: 'Artists', tracks: 'Songs' };
    sfx('play');
    selectTrack(list[0], true, list);
    showToast(`${shuffle ? 'Shuffle Play' : 'Playing'}: ${labelMap[state.tab] || 'Library'}`);
  }

  function syncAlbumViewToggleUI() {
    $c('#btnAlbumViewGrid').classList.toggle('active', state.albumView === 'grid');
    $c('#btnAlbumViewList').classList.toggle('active', state.albumView === 'list');
  }

  function setAlbumView(mode, silent = false) {
    if (mode !== 'grid' && mode !== 'list') return;
    if (mode === state.albumView && !silent) {
      sfx('bump');
      return;
    }
    state.albumView = mode;
    syncAlbumViewToggleUI();
    if (!silent) {
      sfx('tick');
      savePrefs();
      showToast(`Albums: ${mode === 'grid' ? 'Grid View' : 'List View'}`);
    }
    if (state.tab === 'albums' && !state.detailStack.length) {
      renderCatalog();
      scrollToFocusedCatalogItem();
    }
  }

  function syncMinusOneBarUI() {
    const bar = $c('#libCategoryBar');
    const activeSearch = isSearchBarActive();
    bar.classList.toggle('revealed', state.minusOneOpen || activeSearch);
    bar.classList.toggle('searching', activeSearch);
    $c('#btnCatSearchClose').hidden = !activeSearch;
    const placeholders = { albums: 'Search albums...', artists: 'Search artists...', tracks: 'Search songs...', playlists: 'Search playlists...' };
    $c('#catSearchInput').placeholder = placeholders[state.tab] || 'Search...';
    $c('#btnCatPlayAll').classList.toggle('primary', state.tab === 'tracks');
    const controls = getMinusOneControls();
    if (state.minusOneFocusIdx >= controls.length) state.minusOneFocusIdx = controls.length - 1;
    if (state.minusOneFocusIdx < 0) state.minusOneFocusIdx = 0;

    [
      $c('#catSearchBar'),
      $c('#btnCatSearchClose'),
      $c('#btnCatShufflePlay'),
      $c('#btnCatPlayAll'),
      $c('#btnAlbumViewGrid'),
      $c('#btnAlbumViewList'),
      $c('#btnOpenSortSheet'),
      $c('#btnCatNewPlaylist'),
    ].forEach((el) => {
      el.classList.remove('kp-focus');
    });
    if (state.inMinusOne && controls[state.minusOneFocusIdx]) {
      controls[state.minusOneFocusIdx].classList.add('kp-focus');
    }
    syncAlbumViewToggleUI();
  }

  function revealMinusOneBar(focusIt = false) {
    if (state.detailStack.length || !isCatalogTab(state.tab)) return;
    if (!state.minusOneOpen) sfx('tick');
    state.minusOneOpen = true;
    if (focusIt) {
      if (!state.inMinusOne) {
        state.minusOneFocusIdx = getDefaultMinusOneFocusIdx();
      }
      state.inMinusOne = true;
      const controls = getMinusOneControls();
      state.minusOneFocusIdx = Math.min(state.minusOneFocusIdx, controls.length - 1);
    }
    syncMinusOneBarUI();
    if (activeVirtualRender) activeVirtualRender(true);
    else if (state.tab === 'playlists') renderCatalog();
  }

  function hideMinusOneBar(playSfx = false) {
    if (state.searchQuery.trim().length > 0) {
      state.inMinusOne = false;
      syncMinusOneBarUI();
      if (activeVirtualRender) activeVirtualRender(true);
      else if (state.tab === 'playlists') renderCatalog();
      return;
    }
    if (!state.minusOneOpen && !state.inMinusOne) return;
    if (playSfx) sfx('tick');
    state.minusOneOpen = false;
    state.inMinusOne = false;
    state.searchOpen = false;
    syncMinusOneBarUI();
    if (activeVirtualRender) activeVirtualRender(true);
    else if (state.tab === 'playlists') renderCatalog();
  }

  libItemsEl.addEventListener('scroll', () => {
    if (activeVirtualRender) activeVirtualRender(false);
    if (!scrubbingRail) syncScrubberThumbFromScroll();
    if (libItemsEl.scrollTop > 16 && state.minusOneOpen && !state.inMinusOne) {
      hideMinusOneBar(false);
    }
  });

  // Wheel scroll-up at top of list reveals the -1 bar; wheel scroll-down tucks it away!
  libItemsEl.addEventListener('wheel', (e) => {
    cancelSmoothScroll(libItemsEl);
    if (state.detailStack.length || !isCatalogTab(state.tab)) return;
    if (e.deltaY < -8 && libItemsEl.scrollTop <= 2 && !state.minusOneOpen) {
      revealMinusOneBar(false);
    } else if (e.deltaY > 12 && state.minusOneOpen) {
      hideMinusOneBar(false);
    }
  }, { passive: true });

  // Touch / Pointer pull-down at top of list reveals the -1 bar!
  let pullStartY = null;
  libItemsEl.addEventListener('pointerdown', (e) => {
    if (libItemsEl.scrollTop <= 2) pullStartY = e.clientY;
    else pullStartY = null;
  });
  libItemsEl.addEventListener('pointermove', (e) => {
    if (pullStartY === null || state.detailStack.length || !isCatalogTab(state.tab)) return;
    const dy = e.clientY - pullStartY;
    if (dy > 36 && !state.minusOneOpen) {
      revealMinusOneBar(false);
      pullStartY = null;
    } else if (dy < -28 && state.minusOneOpen) {
      hideMinusOneBar(false);
      pullStartY = null;
    }
  });
  libItemsEl.addEventListener('pointerup', () => { pullStartY = null; });
  libItemsEl.addEventListener('pointercancel', () => { pullStartY = null; });

  /* ------------------------------------------------------------------------
     CATALOG RENDERER (ALBUMS / ARTISTS / PLAYLISTS / SONGS + DETAILS)
     ------------------------------------------------------------------------ */
  function buildTrackRow(trk, idx, opts = {}) {
    const isPlaying = trk.id === state.currentTrackId;
    const isFocused = opts.focusedIdx === idx;
    const row = document.createElement('div');
    row.className = `track-row${opts.albumMode ? ' album-track-row' : ''}${isPlaying ? ' playing' : ''}${isFocused ? ' kp-focus' : ''}`;

    if (opts.albumMode) {
      const no = document.createElement('div');
      no.className = `track-no-badge${isPlaying ? ' is-playing' : ''}`;
      if (isPlaying) {
        no.innerHTML = '<svg class="ic-24"><use href="#ic-play"/></svg>';
      } else {
        no.textContent = String(trk.trackNo || idx + 1).padStart(2, '0');
      }
      row.appendChild(no);
    } else {
      const thumbWrap = document.createElement('div');
      thumbWrap.className = 'track-thumb-wrap';
      const thumb = document.createElement('canvas');
      thumb.width = 84;
      thumb.height = 84;
      thumb.className = 'track-thumb';
      drawAlbumArt(thumb, trk);
      thumbWrap.appendChild(thumb);
      if (isPlaying) {
        const badge = document.createElement('span');
        badge.className = 'thumb-play-badge';
        badge.innerHTML = '<svg class="ic-16"><use href="#ic-play"/></svg>';
        thumbWrap.appendChild(badge);
      }
      row.appendChild(thumbWrap);
    }

    const info = document.createElement('div');
    info.className = 'track-meta-main';
    const starMark = trk.starred ? ' ★' : '';
    if (opts.albumMode) {
      // Album Detail stays ultra-clean: title only (format & technical specs live in Get Info!)
      info.innerHTML = `<div class="track-name">${trk.title}${starMark}</div>`;
    } else {
      let subLine;
      if (opts.sortField === 'added') subLine = `${trk.addedAt || ''}  ${trk.artist}`;
      else if (opts.sortField === 'year') subLine = `${trk.year}  ${trk.artist} — ${trk.album}`;
      else subLine = `${trk.artist} — ${trk.album}`;
      info.innerHTML = `<div class="track-name">${trk.title}${starMark}</div><div class="track-sub">${subLine}</div>`;
    }
    row.appendChild(info);

    const dur = document.createElement('div');
    dur.className = 'track-dur';
    dur.textContent = fmtTime(trk.duration);
    row.appendChild(dur);

    const actBtn = document.createElement('button');
    actBtn.className = 'track-q-btn';
    actBtn.title = 'Context Menu (Y)';
    actBtn.innerHTML = '<svg class="ic-28"><use href="#ic-more"/></svg>';
    actBtn.style.display = 'flex';
    actBtn.style.alignItems = 'center';
    actBtn.style.justifyContent = 'center';
    actBtn.addEventListener('click', (ev) => {
      ev.stopPropagation();
      openTrackContextMenu({
        trackId: trk.id,
        source: opts.playlistId ? 'playlist' : 'catalog',
        playlistId: opts.playlistId || null,
      });
    });
    row.appendChild(actBtn);

    row.addEventListener('click', () => {
      sfx('confirm');
      if (opts.isDetail) state.detailFocusIdx = typeof opts.detailFocusValue === 'number' ? opts.detailFocusValue : idx;
      else state.listFocusIdx = idx;
      selectTrack(trk.id, true, opts.playContextIds || null);
    });
    return row;
  }

  function buildAlbumCard(alb, idx, sortField, isFocused = false, isDetail = false) {
    const card = document.createElement('div');
    card.className = `album-card${isFocused ? ' kp-focus' : ''}`;
    const art = document.createElement('canvas');
    art.width = 160;
    art.height = 160;
    drawAlbumArt(art, alb);
    card.appendChild(art);

    const meta = document.createElement('div');
    meta.className = 'album-card-meta';
    meta.innerHTML = `
      <div class="track-name">${alb.title}</div>
      <div class="track-sub">${alb.artist}</div>
      <div class="album-card-count">${alb.tracks.length}</div>
    `;
    card.appendChild(meta);
    card.addEventListener('click', () => {
      if (isDetail) state.detailFocusIdx = idx;
      else state.listFocusIdx = idx;
      goToAlbum(alb.id);
    });
    return card;
  }

  function buildAlbumGridTile(alb, idx, isFocused = false) {
    const tile = document.createElement('div');
    tile.className = `album-grid-tile${isFocused ? ' kp-focus' : ''}`;
    const art = document.createElement('canvas');
    art.width = 240;
    art.height = 240;
    art.className = 'album-grid-art';
    drawAlbumArt(art, alb);
    tile.appendChild(art);

    const meta = document.createElement('div');
    meta.className = 'album-grid-meta';
    meta.innerHTML = `
      <div class="album-grid-title">${alb.title}</div>
      <div class="album-grid-foot">
        <span class="album-grid-artist">${alb.artist}</span>
        <span class="album-card-count">${alb.tracks.length}</span>
      </div>
    `;
    tile.appendChild(meta);
    tile.addEventListener('click', () => {
      state.listFocusIdx = idx;
      goToAlbum(alb.id);
    });
    return tile;
  }

  function buildAlbumListRow(alb, idx, isFocused = false) {
    const row = document.createElement('div');
    row.className = `album-list-row${isFocused ? ' kp-focus' : ''}`;
    const art = document.createElement('canvas');
    art.width = 160;
    art.height = 160;
    art.className = 'album-list-art';
    drawAlbumArt(art, alb);
    row.appendChild(art);

    const meta = document.createElement('div');
    meta.className = 'track-meta-main';
    meta.innerHTML = `
      <div class="track-name">${alb.title}</div>
      <div class="track-sub">${alb.artist}</div>
    `;
    row.appendChild(meta);

    const count = document.createElement('div');
    count.className = 'album-card-count';
    count.textContent = String(alb.tracks.length);
    row.appendChild(count);

    row.addEventListener('click', () => {
      state.listFocusIdx = idx;
      goToAlbum(alb.id);
    });
    return row;
  }

  function renderVirtualizedList(items, rowHeight, cols, renderItemFn) {
    const totalRows = Math.ceil(items.length / cols);
    const totalHeight = totalRows * rowHeight;

    libItemsEl.innerHTML = '';
    const spacer = document.createElement('div');
    spacer.style.position = 'relative';
    spacer.style.width = '100%';
    spacer.style.height = `${totalHeight}px`;
    libItemsEl.appendChild(spacer);

    const renderedRows = new Map();
    let lastStart = -1;
    let lastEnd = -1;
    let lastFocusKey = '';

    const syncVisibleFocus = () => {
      const activeIdx = state.inMinusOne ? -1 : state.listFocusIdx;
      renderedRows.forEach((rowWrap) => {
        for (const child of rowWrap.children) {
          child.classList.toggle('kp-focus', Number(child.dataset.vidx) === activeIdx);
        }
      });
    };

    const paintWindow = (force = false) => {
      const scrollTop = libItemsEl.scrollTop;
      const viewH = libItemsEl.clientHeight || 580;
      const startRow = Math.max(0, Math.floor(scrollTop / rowHeight) - 3);
      const endRow = Math.min(totalRows - 1, Math.ceil((scrollTop + viewH) / rowHeight) + 3);
      const focusKey = `${state.listFocusIdx}:${state.inMinusOne}`;

      const rangeChanged = startRow !== lastStart || endRow !== lastEnd;
      if (!force && !rangeChanged && focusKey === lastFocusKey) return;

      if (rangeChanged || force) {
        // Remove only rows that left the overscan window
        for (const [r, el] of renderedRows.entries()) {
          if (r < startRow || r > endRow) {
            el.remove();
            renderedRows.delete(r);
          }
        }
        // Mount only newly entered rows (zero redraw of existing cover canvases!)
        for (let r = startRow; r <= endRow; r++) {
          if (renderedRows.has(r)) continue;
          const rowWrap = document.createElement('div');
          rowWrap.style.position = 'absolute';
          rowWrap.style.left = '0';
          rowWrap.style.right = '0';
          rowWrap.style.top = `${r * rowHeight}px`;
          if (cols >= 2) {
            rowWrap.style.display = 'grid';
            rowWrap.style.gridTemplateColumns = `repeat(${cols}, 1fr)`;
            rowWrap.style.gap = '14px';
          }
          for (let c = 0; c < cols; c++) {
            const idx = r * cols + c;
            if (idx < items.length) {
              const itemEl = renderItemFn(items[idx], idx);
              itemEl.dataset.vidx = String(idx);
              rowWrap.appendChild(itemEl);
            }
          }
          spacer.appendChild(rowWrap);
          renderedRows.set(r, rowWrap);
        }
        lastStart = startRow;
        lastEnd = endRow;
      }

      if (focusKey !== lastFocusKey || force) {
        syncVisibleFocus();
        lastFocusKey = focusKey;
      }
    };

    activeVirtualRender = paintWindow;
    paintWindow(true);
    return totalHeight;
  }

  function shuffleTrackIds(ids) {
    const copy = [...ids];
    for (let i = copy.length - 1; i > 0; i--) {
      const j = Math.floor(Math.random() * (i + 1));
      [copy[i], copy[j]] = [copy[j], copy[i]];
    }
    return copy;
  }

  function renderCatalog(scrollDuration = 165) {
    activeVirtualRender = null;
    const catBar = $c('#libCategoryBar');
    const catPlayActions = $c('#catPlayActions');
    const albumViewToggle = $c('#albumViewToggle');
    const sortControls = $c('#catSortControls');
    const detailBar = $c('#libDetailBar');
    const detailArt = $c('#detailArtCanvas');
    const newPlHeaderBtn = $c('#btnCatNewPlaylist');

    const detail = state.detailStack[state.detailStack.length - 1];
    const renderKey = detail ? `${detail.type}:${detail.id}` : `root:${state.tab}:${state.albumView}`;
    const sameView = renderKey === lastCatalogRenderKey;
    const prevScrollTop = sameView ? libItemsEl.scrollTop : 0;
    lastCatalogRenderKey = renderKey;
    cancelSmoothScroll(libItemsEl);

    libItemsEl.className = 'lib-items';
    libItemsEl.innerHTML = '';
    detailArt.hidden = false;
    detailArt.classList.remove('round-avatar');
    catPlayActions.hidden = true;
    albumViewToggle.hidden = true;
    newPlHeaderBtn.hidden = true;
    sortControls.hidden = true;
    smartScrubberEl.hidden = true;
    scrubberPopupEl.hidden = true;
    state.activeDetailAlbums = [];
    state.activeDetailTracks = [];

    // 1) DETAIL DRILL-DOWN VIEW (ALBUM / ARTIST / PLAYLIST)
    if (detail) {
      catBar.hidden = true;
      detailBar.hidden = false;
      $c('#btnDetailShufflePlay').classList.toggle('kp-focus', state.detailFocusIdx === -3);
      const detailPlay = $c('#btnDetailPlayAll');
      detailPlay.classList.toggle('primary', detail.type === 'playlist');
      detailPlay.classList.toggle('kp-focus', state.detailFocusIdx === -2);
      $c('#btnDetailQueueAll').classList.toggle('kp-focus', state.detailFocusIdx === -1);

      if (detail.type === 'album') {
        const alb = getAlbumById(detail.id);
        if (!alb) { popDetail(); return; }
        drawAlbumArt(detailArt, alb);
        $c('#detailTitle').textContent = alb.title;
        const discPart = alb.discCount > 1 ? `${alb.discCount} Discs, ` : '';
        $c('#detailSub').textContent = `${alb.artist} (${alb.year}) — ${discPart}${alb.tracks.length} ${alb.tracks.length === 1 ? 'track' : 'tracks'}`;

        const trackIds = alb.tracks.map((t) => t.id);
        state.activeDetailTracks = alb.tracks;
        $c('#btnDetailShufflePlay').onclick = () => {
          if (!trackIds.length) return;
          const shuffled = shuffleTrackIds(trackIds);
          sfx('play');
          selectTrack(shuffled[0], true, shuffled);
          showToast(`Shuffle Play: ${alb.title}`);
        };
        $c('#btnDetailPlayAll').onclick = () => {
          sfx('play');
          selectTrack(trackIds[0], true, trackIds);
          showToast(`Playing Album: ${alb.title}`);
        };
        $c('#btnDetailQueueAll').onclick = () => {
          sfx('add');
          state.queue.push(...trackIds);
          syncTrackUI();
          renderQueue();
          showToast('Added Album to Queue');
        };

        let lastDisc = 0;
        let focusedRowEl = null;
        alb.tracks.forEach((trk, idx) => {
          const dNo = trk.discNo || 1;
          if (alb.discCount > 1 && dNo !== lastDisc) {
            lastDisc = dNo;
            const div = document.createElement('div');
            div.className = 'disc-divider';
            div.textContent = `DISC ${dNo}`;
            libItemsEl.appendChild(div);
          }
          const rowEl = buildTrackRow(trk, idx, {
            albumMode: true,
            isDetail: true,
            focusedIdx: state.detailFocusIdx,
            playContextIds: trackIds,
          });
          if (idx === state.detailFocusIdx) focusedRowEl = rowEl;
          libItemsEl.appendChild(rowEl);
        });
        libItemsEl.scrollTop = prevScrollTop;
        configureSmartScrubber('detail', alb.tracks, { mode: 'scroll', field: 'index' }, libItemsEl.scrollHeight);
        if (focusedRowEl) smoothScrollIntoView(libItemsEl, focusedRowEl, scrollDuration);
        else if (state.detailFocusIdx < 0) smoothScrollTo(libItemsEl, 0, scrollDuration);
        return;
      }

      if (detail.type === 'artist') {
        const art = getArtistByName(detail.id);
        if (!art) { popDetail(); return; }
        detailArt.hidden = true;
        $c('#detailTitle').textContent = art.name;
        $c('#detailSub').textContent = `${art.albums.length} ${art.albums.length === 1 ? 'Album' : 'Albums'}, ${art.tracks.length} ${art.tracks.length === 1 ? 'Song' : 'Songs'}`;

        const trackSlice = art.tracks.slice(0, 60);
        const trackIds = trackSlice.map((t) => t.id);
        state.activeDetailTracks = trackSlice;
        $c('#btnDetailShufflePlay').onclick = () => {
          if (!trackIds.length) return;
          const shuffled = shuffleTrackIds(trackIds);
          sfx('play');
          selectTrack(shuffled[0], true, shuffled);
          showToast(`Shuffle Play: ${art.name}`);
        };
        $c('#btnDetailPlayAll').onclick = () => {
          sfx('play');
          selectTrack(trackIds[0], true, trackIds);
          showToast(`Playing: ${art.name}`);
        };
        $c('#btnDetailQueueAll').onclick = () => {
          sfx('add');
          state.queue.push(...trackIds);
          syncTrackUI();
          renderQueue();
          showToast(`Queued: ${art.name}`);
        };

        const artistAlbums = art.albums;
        const albCount = artistAlbums.length;
        state.activeDetailAlbums = artistAlbums;

        const albLabel = document.createElement('div');
        albLabel.className = 'detail-section-label';
        albLabel.textContent = `ALBUMS (${albCount})`;
        libItemsEl.appendChild(albLabel);

        let focusedEl = null;
        const strip = document.createElement('div');
        strip.className = 'artist-albums-strip';
        artistAlbums.forEach((alb, i) => {
          const isFocused = state.detailFocusIdx === i;
          const cardEl = buildAlbumCard(alb, i, 'year', isFocused, true);
          if (isFocused) focusedEl = cardEl;
          strip.appendChild(cardEl);
        });
        libItemsEl.appendChild(strip);

        const songLabel = document.createElement('div');
        songLabel.className = 'detail-section-label';
        songLabel.textContent = `SONGS (${art.tracks.length})`;
        libItemsEl.appendChild(songLabel);

        trackSlice.forEach((trk, idx) => {
          const globalIdx = albCount + idx;
          const rowEl = buildTrackRow(trk, idx, {
            isDetail: true,
            detailFocusValue: globalIdx,
            focusedIdx: state.detailFocusIdx - albCount,
            playContextIds: trackIds,
          });
          if (state.detailFocusIdx === globalIdx) focusedEl = rowEl;
          libItemsEl.appendChild(rowEl);
        });
        libItemsEl.scrollTop = prevScrollTop;
        configureSmartScrubber('detail', [...artistAlbums, ...trackSlice], { mode: 'scroll', field: 'index' }, libItemsEl.scrollHeight);
        if (focusedEl) smoothScrollIntoView(libItemsEl, focusedEl, scrollDuration);
        else if (state.detailFocusIdx < 0) smoothScrollTo(libItemsEl, 0, scrollDuration);
        return;
      }

      if (detail.type === 'playlist') {
        const isStarred = detail.id === '__starred__';
        const pl = isStarred
          ? { id: '__starred__', name: '★ Starred', trackIds: getStarredTrackIds() }
          : state.playlists.find((p) => p.id === detail.id);
        if (!pl) { popDetail(); return; }

        drawAlbumArt(detailArt, { artStyle: isStarred ? 'starred-badge' : 'playlist-badge', colors: ['#ff7a1a', '#141923', '#f2efe9'] });
        $c('#detailTitle').textContent = pl.name;
        const tracks = pl.trackIds.map((id) => getTrackById(id)).filter(Boolean);
        state.activeDetailTracks = tracks;
        const totalSec = tracks.reduce((acc, t) => acc + t.duration, 0);
        $c('#detailSub').textContent = `${tracks.length} songs — ${fmtTime(totalSec)}`;

        $c('#btnDetailShufflePlay').onclick = () => {
          if (!pl.trackIds.length) return;
          const shuffled = shuffleTrackIds(pl.trackIds);
          sfx('play');
          selectTrack(shuffled[0], true, shuffled);
          showToast(`Shuffle Play: ${pl.name}`);
        };
        $c('#btnDetailPlayAll').onclick = () => {
          if (!pl.trackIds.length) return;
          sfx('play');
          selectTrack(pl.trackIds[0], true, pl.trackIds);
          showToast(`Playing: ${pl.name}`);
        };
        $c('#btnDetailQueueAll').onclick = () => {
          if (!pl.trackIds.length) return;
          sfx('add');
          state.queue.push(...pl.trackIds);
          syncTrackUI();
          renderQueue();
          showToast('Added Playlist to Queue');
        };

        if (!tracks.length) {
          const empty = document.createElement('div');
          empty.className = 'module-card';
          empty.style.cssText = 'text-align:center; padding:48px 24px; font-size:var(--t-body); color:var(--text-dim);';
          empty.textContent = isStarred ? 'No starred songs yet. Star any track to see it here.' : 'Playlist is empty. Press Y on any track to add.';
          libItemsEl.appendChild(empty);
          configureSmartScrubber('detail', [], { mode: 'scroll', field: 'index' }, 0);
        } else {
          let focusedPlRow = null;
          tracks.forEach((trk, idx) => {
            const rowEl = buildTrackRow(trk, idx, {
              isDetail: true,
              focusedIdx: state.detailFocusIdx,
              playlistId: pl.id,
              playContextIds: pl.trackIds,
            });
            if (idx === state.detailFocusIdx) focusedPlRow = rowEl;
            libItemsEl.appendChild(rowEl);
          });
          libItemsEl.scrollTop = prevScrollTop;
          configureSmartScrubber('detail', tracks, { mode: 'scroll', field: 'index' }, libItemsEl.scrollHeight);
          if (focusedPlRow) smoothScrollIntoView(libItemsEl, focusedPlRow, scrollDuration);
          else if (state.detailFocusIdx < 0) smoothScrollTo(libItemsEl, 0, scrollDuration);
        }
        return;
      }
    }

    // 2) ROOT CATEGORY VIEWS (OPTION 1 DIRECT TABS + -1 PULL/PRESS-UP UTILITY BAR)
    catBar.hidden = false;
    detailBar.hidden = true;
    syncMinusOneBarUI();

    const listFocusActive = !state.inMinusOne ? state.listFocusIdx : -1;

    if (state.tab === 'albums' || state.tab === 'tracks' || state.tab === 'artists') {
      const sortMeta = getSortOptionMeta(state.tab);
      const q = state.searchQuery.trim().toLowerCase();
      let sorted = getSortedItems(state.tab);
      if (q) {
        if (state.tab === 'albums') {
          sorted = sorted.filter((a) => a.title.toLowerCase().includes(q) || a.artist.toLowerCase().includes(q));
        } else if (state.tab === 'artists') {
          sorted = sorted.filter((a) => a.name.toLowerCase().includes(q));
        } else if (state.tab === 'tracks') {
          sorted = sorted.filter((t) => t.title.toLowerCase().includes(q) || t.artist.toLowerCase().includes(q) || t.album.toLowerCase().includes(q));
        }
      }
      state.activeSortedItems = sorted;
      if (state.listFocusIdx >= sorted.length) state.listFocusIdx = Math.max(0, sorted.length - 1);

      catPlayActions.hidden = false;
      sortControls.hidden = false;
      $c('#sortFieldLabel').textContent = `${sortMeta.label} ${sortMeta.dir === 'asc' ? '↑' : '↓'}`;

      if (q && !sorted.length) {
        $c('#catHeaderCount').textContent = '0';
        libItemsEl.innerHTML = '<div class="queue-empty">No matches</div>';
        configureSmartScrubber(state.tab, [], sortMeta, 0);
        return;
      }

      if (state.tab === 'albums') {
        albumViewToggle.hidden = false;
        syncAlbumViewToggleUI();
        $c('#catHeaderCount').textContent = sorted.length.toLocaleString();
        const { cols, rowH } = getAlbumsLayout();
        const h = renderVirtualizedList(sorted, rowH, cols, (alb, idx) => {
          const focused = !state.inMinusOne && idx === state.listFocusIdx;
          return state.albumView === 'list'
            ? buildAlbumListRow(alb, idx, focused)
            : buildAlbumGridTile(alb, idx, focused);
        });
        configureSmartScrubber('albums', sorted, sortMeta, h);
      } else if (state.tab === 'tracks') {
        $c('#catHeaderCount').textContent = sorted.length.toLocaleString();
        const h = renderVirtualizedList(sorted, 128, 1, (trk, idx) =>
          buildTrackRow(trk, idx, { focusedIdx: !state.inMinusOne ? state.listFocusIdx : -1, sortField: sortMeta.field })
        );
        configureSmartScrubber('tracks', sorted, sortMeta, h);
      } else if (state.tab === 'artists') {
        $c('#catHeaderCount').textContent = sorted.length.toLocaleString();
        const h = renderVirtualizedList(sorted, 108, 1, (art, idx) => {
          const row = document.createElement('div');
          row.className = `artist-row${!state.inMinusOne && idx === state.listFocusIdx ? ' kp-focus' : ''}`;

          const meta = document.createElement('div');
          meta.className = 'track-meta-main';
          const albTxt = `${art.albums.length} ${art.albums.length === 1 ? 'Album' : 'Albums'}`;
          const songTxt = `${art.tracks.length} ${art.tracks.length === 1 ? 'Song' : 'Songs'}`;
          meta.innerHTML = `<div class="track-name">${art.name}</div><div class="track-sub">${albTxt}, ${songTxt}</div>`;
          row.appendChild(meta);

          row.addEventListener('click', () => {
            state.listFocusIdx = idx;
            goToArtist(art.name);
          });
          return row;
        });
        configureSmartScrubber('artists', sorted, sortMeta, h);
      }
    } else if (state.tab === 'playlists') {
      const q = state.searchQuery.trim().toLowerCase();
      let allPls = [
        { id: '__starred__', name: 'Starred', isStarred: true, count: getStarredTrackIds().length },
        ...state.playlists.map((p) => ({ id: p.id, name: p.name, isStarred: false, count: p.trackIds.length })),
      ];
      if (q) allPls = allPls.filter((pl) => pl.name.toLowerCase().includes(q));
      state.activeSortedItems = allPls;
      if (state.listFocusIdx >= allPls.length) state.listFocusIdx = Math.max(0, allPls.length - 1);

      $c('#catHeaderCount').textContent = String(allPls.length);
      newPlHeaderBtn.hidden = false;

      if (!allPls.length) {
        libItemsEl.innerHTML = '<div class="queue-empty">No matches</div>';
        configureSmartScrubber('playlists', [], { mode: 'scroll', field: 'index' }, 0);
        return;
      }

      allPls.forEach((pl, idx) => {
        const row = document.createElement('div');
        row.className = `playlist-row${idx === listFocusActive ? ' kp-focus' : ''}`;
        row.innerHTML = `
          <div class="pl-icon-box">${pl.isStarred ? '★' : '♫'}</div>
          <div class="track-meta-main">
            <div class="track-name">${pl.name}</div>
          </div>
          <div class="album-card-count">${pl.count}</div>
        `;
        row.addEventListener('click', () => {
          state.listFocusIdx = idx;
          pushDetail({ type: 'playlist', id: pl.id });
        });
        libItemsEl.appendChild(row);
      });
      libItemsEl.scrollTop = prevScrollTop;
      configureSmartScrubber('playlists', allPls, { mode: 'scroll', field: 'index' }, libItemsEl.scrollHeight);
    }
  }

  /* ------------------------------------------------------------------------
     TAB 06: QUEUE (PLAY-NEXT LINE-ABOVE-ARROW ICON — NO DOWN ARROW!)
     ------------------------------------------------------------------------ */
  function renderQueue(scrollDuration = 0) {
    const list = $c('#queueList');
    const prevScroll = list.scrollTop;
    cancelSmoothScroll(list);
    list.innerHTML = '';

    if (state.queue.length === 0) {
      list.innerHTML = '<div class="queue-empty">Up Next is empty</div>';
      configureQueueScrubber();
      return;
    }

    if (state.queueFocusIdx >= state.queue.length) {
      state.queueFocusIdx = Math.max(0, state.queue.length - 1);
    }

    state.queue.forEach((id, idx) => {
      const trk = getTrackById(id);
      const isFocused = idx === state.queueFocusIdx;
      const row = document.createElement('div');
      row.className = `queue-row${isFocused ? ' kp-focus' : ''}`;
      row.innerHTML = `
        <div class="q-info">
          <div class="track-name">${trk.title}${trk.starred ? ' ★' : ''}</div>
          <div class="track-sub">${trk.artist} — ${trk.album}</div>
        </div>
        <span class="track-dur">${fmtTime(trk.duration)}</span>
        <div class="q-actions">
          <button class="q-mini-btn" data-act="playnext" title="Play Next" aria-label="Play Next" style="display:flex;align-items:center;justify-content:center;"><svg class="ic-24"><use href="#ic-play-next"/></svg></button>
          <button class="q-mini-btn" data-act="menu" title="Context Menu (Y)" aria-label="Context Menu" style="display:flex;align-items:center;justify-content:center;"><svg class="ic-24"><use href="#ic-more"/></svg></button>
        </div>
      `;
      row.querySelector('.q-info').addEventListener('click', () => {
        sfx('confirm');
        state.queueFocusIdx = idx;
        playFromQueue(idx);
      });
      row.querySelector('[data-act="playnext"]').addEventListener('click', (ev) => {
        ev.stopPropagation();
        playNextInQueue(id, idx);
      });
      row.querySelector('[data-act="menu"]').addEventListener('click', (ev) => {
        ev.stopPropagation();
        state.queueFocusIdx = idx;
        openTrackContextMenu({ trackId: id, source: 'queue', queueIdx: idx });
      });
      list.appendChild(row);
    });
    list.scrollTop = prevScroll;
    configureQueueScrubber();
    if (scrollDuration > 0 && list.children[state.queueFocusIdx]) {
      smoothScrollIntoView(list, list.children[state.queueFocusIdx], scrollDuration);
    }
  }

  /* ------------------------------------------------------------------------
     TOP STAGE LIVE RENDERERS
     ------------------------------------------------------------------------ */
  const vuCanvas = $s('#vuMeterCanvas');
  const vuCtx = vuCanvas.getContext('2d');
  const specCanvas = $s('#spectrumCanvas');
  const specCtx = specCanvas.getContext('2d');

  function drawSingleVU(ctx, x, y, w, h, level, peak, label, tc) {
    const lvl = Math.min(1, Math.max(0, level));
    const pk = Math.min(1, Math.max(0, peak));
    const cx = x + w / 2;
    const cy = y + h - 34;
    const startAng = -Math.PI * 0.70;
    const endAng = -Math.PI * 0.30;
    const sweep = endAng - startAng;
    const redT = 0.72;
    const redAng = startAng + redT * sweep;

    const scaleMarks = [
      { t: 0.00, text: '-20', red: false },
      { t: 0.22, text: '-10', red: false },
      { t: 0.36, text: '-7',  red: false },
      { t: 0.55, text: '-3',  red: false },
      { t: 0.72, text: '0',   red: true },
      { t: 1.00, text: '+3',  red: true },
    ];

    ctx.save();

    if (state.theme === 'vfd') {
      // Authentic Segmented Vacuum Fluorescent VU Meter Display (clipped & zero overflow)
      ctx.beginPath();
      ctx.rect(x + 1, y + 1, w - 2, h - 2);
      ctx.fillStyle = '#040e12';
      ctx.fill();

      ctx.save();
      ctx.beginPath();
      ctx.rect(x + 2, y + 2, w - 4, h - 4);
      ctx.clip();

      // Subtle unlit dot-matrix grid backdrop (locked to 8px = 4×2px VFD grid)
      ctx.fillStyle = '#081b21';
      for (let gx = x + 8; gx < x + w - 8; gx += 8) {
        for (let gy = y + 8; gy < y + h - 8; gy += 8) {
          ctx.fillRect(gx, gy, 4, 4);
        }
      }

      const r = 324;
      const segs = 24;
      const litCount = Math.round(lvl * segs);
      const peakIdx = pk > 0.04 ? Math.min(segs - 1, Math.floor(pk * segs)) : -1;

      for (let s = 0; s < segs; s++) {
        const a0 = startAng + (s / segs) * sweep;
        const a1 = startAng + ((s + 0.70) / segs) * sweep;
        const isRed = (s / segs) >= redT;
        const isPeak = s === peakIdx && s >= litCount;
        const isLit = s < litCount;
        ctx.strokeStyle = isPeak
          ? (isRed ? tc.danger : tc.text)
          : isLit
            ? (isRed ? tc.danger : tc.accent)
            : (isRed ? 'rgba(255, 74, 94, 0.16)' : 'rgba(56, 242, 198, 0.12)');
        ctx.lineWidth = 24;
        ctx.lineCap = 'butt';
        ctx.beginPath();
        ctx.arc(cx, cy, r, a0, a1);
        ctx.stroke();
      }

      // Scale ticks & numbers strictly inside safe radius
      ctx.font = '400 20px "PixelMplus10", "DotGothic16", monospace';
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      scaleMarks.forEach((m) => {
        const ang = startAng + m.t * sweep;
        ctx.fillStyle = m.red ? tc.danger : tc.text;
        ctx.fillText(m.text, cx + Math.cos(ang) * (r - 44), cy + Math.sin(ang) * (r - 44));
      });

      // Top-left Channel label
      ctx.textAlign = 'left';
      ctx.textBaseline = 'middle';
      ctx.fillStyle = tc.textDim;
      ctx.font = '400 20px "PixelMplus10", "DotGothic16", monospace';
      ctx.fillText(label, x + 24, y + 36);

      // Top-right VFD PEAK indicator block
      const isOverload = lvl > 0.84;
      ctx.fillStyle = isOverload ? tc.danger : 'rgba(255, 74, 94, 0.18)';
      ctx.fillRect(x + w - 88, y + 22, 64, 28);
      ctx.fillStyle = isOverload ? '#040e12' : 'rgba(255, 74, 94, 0.45)';
      ctx.font = '400 16px "DotGothic16", monospace';
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      ctx.fillText('PEAK', x + w - 56, y + 36);

      // Center VU emblem
      ctx.fillStyle = tc.text;
      ctx.font = '400 32px "DotGothic16", monospace';
      ctx.fillText('VU', cx, y + 360);
      ctx.fillStyle = tc.textDim;
      ctx.font = '400 16px "DotGothic16", monospace';
      ctx.fillText('dB', cx, y + 394);

      ctx.restore();

      // Crisp 2px outer VFD border
      ctx.strokeStyle = tc.borderStrong;
      ctx.lineWidth = 2;
      ctx.strokeRect(x + 1, y + 1, w - 2, h - 2);
      ctx.restore();
      return;
    }

    // OBSIDIAN & CERAMIC — Backlit Studio Analog VU Meter (clipped cleanly inside frame)
    const isCeramic = state.theme === 'ceramic';
    const radius = 16;

    const grad = ctx.createRadialGradient(cx, y + h * 0.92, 16, cx, y + h * 0.52, w * 0.72);
    if (isCeramic) {
      grad.addColorStop(0, '#fffdf7');
      grad.addColorStop(0.55, tc.vuBg);
      grad.addColorStop(1, '#ddd4c2');
    } else {
      grad.addColorStop(0, tc.vuGlow);
      grad.addColorStop(0.42, tc.vuBg);
      grad.addColorStop(1, '#0e0c0a');
    }

    ctx.beginPath();
    ctx.roundRect(x + 1, y + 1, w - 2, h - 2, radius);
    ctx.fillStyle = grad;
    ctx.fill();

    // Clip all interior meter graphics so nothing can ever bleed out of the bezel
    ctx.save();
    ctx.beginPath();
    ctx.roundRect(x + 2, y + 2, w - 4, h - 4, radius - 1);
    ctx.clip();

    const r = 336;
    const inkMain = isCeramic ? '#2c2720' : '#f5e4c8';
    const inkSoft = isCeramic ? 'rgba(44, 39, 32, 0.55)' : 'rgba(255, 228, 185, 0.55)';

    // Main scale arc (-20 .. 0 dB)
    ctx.lineWidth = 5;
    ctx.lineCap = 'butt';
    ctx.strokeStyle = isCeramic ? '#3a342a' : 'rgba(255, 228, 185, 0.78)';
    ctx.beginPath();
    ctx.arc(cx, cy, r, startAng, redAng);
    ctx.stroke();

    // Red overload arc (0 .. +3 dB)
    ctx.strokeStyle = tc.danger;
    ctx.lineWidth = 8;
    ctx.beginPath();
    ctx.arc(cx, cy, r, redAng, endAng);
    ctx.stroke();

    // Fine minor ticks along the scale
    const minorCount = 20;
    for (let i = 1; i < minorCount; i++) {
      const t = i / minorCount;
      const ang = startAng + t * sweep;
      const isRed = t >= redT;
      ctx.strokeStyle = isRed ? tc.danger : inkSoft;
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.moveTo(cx + Math.cos(ang) * (r - 10), cy + Math.sin(ang) * (r - 10));
      ctx.lineTo(cx + Math.cos(ang) * (r + 5), cy + Math.sin(ang) * (r + 5));
      ctx.stroke();
    }

    // Major labeled ticks
    ctx.font = '700 20px "IBM Plex Mono", monospace';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    scaleMarks.forEach((m) => {
      const ang = startAng + m.t * sweep;
      const col = m.red ? tc.danger : inkMain;
      ctx.strokeStyle = col;
      ctx.lineWidth = 3;
      ctx.beginPath();
      ctx.moveTo(cx + Math.cos(ang) * (r - 16), cy + Math.sin(ang) * (r - 16));
      ctx.lineTo(cx + Math.cos(ang) * (r + 12), cy + Math.sin(ang) * (r + 12));
      ctx.stroke();

      ctx.fillStyle = col;
      ctx.fillText(m.text, cx + Math.cos(ang) * (r - 44), cy + Math.sin(ang) * (r - 44));
    });

    // Top-left Channel label
    ctx.textAlign = 'left';
    ctx.textBaseline = 'middle';
    ctx.fillStyle = inkSoft;
    ctx.font = '700 18px "IBM Plex Mono", monospace';
    ctx.fillText(label, x + 28, y + 36);

    // Top-right Peak LED
    const isPeakLit = lvl > 0.84;
    if (isPeakLit) {
      ctx.save();
      ctx.shadowColor = tc.danger;
      ctx.shadowBlur = 16;
      ctx.beginPath();
      ctx.arc(x + w - 34, y + 36, 10, 0, Math.PI * 2);
      ctx.fillStyle = tc.danger;
      ctx.fill();
      ctx.restore();
    } else {
      ctx.beginPath();
      ctx.arc(x + w - 34, y + 36, 10, 0, Math.PI * 2);
      ctx.fillStyle = isCeramic ? '#cdc4b2' : '#2a2220';
      ctx.fill();
    }

    // Center VU emblem
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillStyle = isCeramic ? '#2c2720' : 'rgba(255, 224, 180, 0.9)';
    ctx.font = '700 32px "Space Grotesk", sans-serif';
    ctx.fillText('VU', cx, y + 344);
    ctx.fillStyle = inkSoft;
    ctx.font = '600 18px "IBM Plex Mono", monospace';
    ctx.fillText('dB', cx, y + 380);

    // Analog Needle with soft dial shadow
    const needleAng = startAng + lvl * sweep;
    const tipX = cx + Math.cos(needleAng) * (r + 8);
    const tipY = cy + Math.sin(needleAng) * (r + 8);

    ctx.save();
    ctx.strokeStyle = 'rgba(0, 0, 0, 0.28)';
    ctx.lineWidth = 6;
    ctx.lineCap = 'round';
    ctx.beginPath();
    ctx.moveTo(cx + 4, cy + 4);
    ctx.lineTo(tipX + 4, tipY + 4);
    ctx.stroke();
    ctx.restore();

    ctx.strokeStyle = tc.accent;
    ctx.lineWidth = 5;
    ctx.lineCap = 'round';
    ctx.beginPath();
    ctx.moveTo(cx, cy);
    ctx.lineTo(tipX, tipY);
    ctx.stroke();

    // Lower pivot housing cap (cleanly clipped at bezel bottom)
    ctx.fillStyle = isCeramic ? '#2c2720' : '#18191d';
    ctx.beginPath();
    ctx.arc(cx, cy, 40, Math.PI, 0);
    ctx.fill();
    ctx.strokeStyle = isCeramic ? 'rgba(255,255,255,0.18)' : 'rgba(255,255,255,0.12)';
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.arc(cx, cy, 40, Math.PI, 0);
    ctx.stroke();

    ctx.restore();

    // Crisp outer 2px bezel border (drawn after unclip so corners stay razor-sharp)
    ctx.strokeStyle = tc.borderStrong;
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.roundRect(x + 1, y + 1, w - 2, h - 2, radius);
    ctx.stroke();

    ctx.restore();
  }

  function drawSpectrumStage(tc, fft, wave) {
    const w = specCanvas.width;
    const h = specCanvas.height;

    specCtx.fillStyle = tc.bg;
    specCtx.fillRect(0, 0, w, h);

    if (state.theme === 'vfd') {
      // Authentic Pioneer / Sony ES Graphic VFD Dot-Matrix Spectrum + Quantized Osc!
      // 1) Dot-matrix stepped oscilloscope across top
      const oscCell = 8;
      const oscCols = Math.floor((w - 48) / oscCell);
      const oscBaseY = 112;
      for (let c = 0; c < oscCols; c++) {
        const wIdx = Math.floor((c / oscCols) * (wave.length - 1));
        const val = (wave[wIdx] - 128) / 128;
        const y = Math.round((oscBaseY + val * 74) / oscCell) * oscCell;
        specCtx.fillStyle = '#092026';
        specCtx.fillRect(24 + c * oscCell, oscBaseY, 6, 6);
        specCtx.fillStyle = tc.mint;
        specCtx.fillRect(24 + c * oscCell, y, 6, 6);
      }

      // 2) 28-band segmented VFD phosphor columns with unlit dark-teal matrix cells
      const bars = 28;
      const padX = 24;
      const baseLine = h - 138;
      const segRows = 22;
      const segPitchY = 16;
      const segH = 11;
      const colW = (w - padX * 2) / bars;

      for (let i = 0; i < bars; i++) {
        const bin = Math.min(fft.length - 1, Math.floor(Math.pow(i / bars, 1.55) * (fft.length * 0.55)) + 1);
        const val = fft[bin] / 255;
        const litRows = Math.max(1, Math.round(val * segRows));

        const peakRows = Math.round((state.specPeaks[i] / (segRows * segPitchY)) * segRows);
        const nextPeak = Math.max(litRows, Math.max(1, peakRows - 0.25));
        state.specPeaks[i] = nextPeak * segPitchY;
        const peakRowIdx = Math.min(segRows - 1, Math.floor(nextPeak));

        const x = Math.round(padX + i * colW + 4);
        const bw = Math.round(colW - 8);

        for (let r = 0; r < segRows; r++) {
          const sy = baseLine - (r + 1) * segPitchY;
          const isClip = r >= segRows - 4;
          const isPeak = r === peakRowIdx && litRows > 1;
          const isLit = r < litRows;
          if (isPeak) {
            specCtx.fillStyle = isClip ? tc.danger : tc.text;
          } else if (isLit) {
            specCtx.fillStyle = isClip ? tc.danger : tc.accent;
          } else {
            specCtx.fillStyle = isClip ? 'rgba(255, 74, 94, 0.1)' : '#092128';
          }
          specCtx.fillRect(x, sy, bw, segH);
        }
      }
      return;
    }

    const isCeramic = state.theme === 'ceramic';
    const oscY = 112;

    // Subtle studio reference baseline for oscilloscope
    specCtx.fillStyle = tc.border;
    specCtx.fillRect(24, oscY - 1, w - 48, 2);

    specCtx.save();
    specCtx.beginPath();
    for (let i = 0; i < wave.length; i++) {
      const x = 24 + (i / (wave.length - 1)) * (w - 48);
      const y = oscY + ((wave[i] - 128) / 128) * 74;
      if (i === 0) specCtx.moveTo(x, y);
      else specCtx.lineTo(x, y);
    }
    specCtx.strokeStyle = isCeramic ? 'rgba(22, 23, 27, 0.72)' : 'rgba(245, 242, 234, 0.78)';
    specCtx.lineWidth = 3;
    specCtx.lineJoin = 'round';
    specCtx.lineCap = 'round';
    specCtx.stroke();
    specCtx.restore();

    const bars = 28;
    const padX = 24;
    const baseLine = h - 138;
    const maxH = 344;
    const colW = (w - padX * 2) / bars;

    // Subtle spectrum baseline rule
    specCtx.fillStyle = tc.border;
    specCtx.fillRect(padX, baseLine, w - padX * 2, 2);

    const barGrad = specCtx.createLinearGradient(0, baseLine - maxH, 0, baseLine);
    if (isCeramic) {
      barGrad.addColorStop(0, tc.accent);
      barGrad.addColorStop(0.28, '#3f424a');
      barGrad.addColorStop(0.72, '#8b857a');
      barGrad.addColorStop(1, '#c8c0b2');
    } else {
      barGrad.addColorStop(0, tc.accent);
      barGrad.addColorStop(0.32, '#8a92a4');
      barGrad.addColorStop(0.72, '#414859');
      barGrad.addColorStop(1, '#222630');
    }

    for (let i = 0; i < bars; i++) {
      const bin = Math.min(fft.length - 1, Math.floor(Math.pow(i / bars, 1.55) * (fft.length * 0.55)) + 1);
      const val = fft[bin] / 255;
      const barH = Math.max(8, val * maxH);

      if (barH > state.specPeaks[i]) state.specPeaks[i] = barH;
      else state.specPeaks[i] = Math.max(8, state.specPeaks[i] - 3);

      const x = padX + i * colW + 4;
      const bw = colW - 8;

      specCtx.fillStyle = barGrad;
      specCtx.beginPath();
      specCtx.roundRect(x, baseLine - barH, bw, barH, [4, 4, 0, 0]);
      specCtx.fill();

      specCtx.fillStyle = state.specPeaks[i] > maxH * 0.62 ? tc.accent : tc.textDim;
      specCtx.fillRect(x, baseLine - state.specPeaks[i] - 8, bw, 4);
    }
  }

  /* ------------------------------------------------------------------------
     MAIN 60 FPS LOOP
     ------------------------------------------------------------------------ */
  function renderLoop() {
    const tc = getThemeColors();
    audio.tickTime(() => nextTrack(true));

    const curFmt = fmtTime(audio.currentTime);
    const totFmt = fmtTime(audio.duration);
    const pct = audio.duration > 0 ? Math.min(100, (audio.currentTime / audio.duration) * 100) : 0;

    $c('#timeCurrent').textContent = curFmt;
    $s('#stageProgressFill').style.width = `${pct.toFixed(2)}%`;
    $s('#stageTimeCur').textContent = curFmt;

    const { left, right } = audio.getStereoLevels();
    state.vuNeedleL += (left - state.vuNeedleL) * (left > state.vuNeedleL ? 0.35 : 0.1);
    state.vuNeedleR += (right - state.vuNeedleR) * (right > state.vuNeedleR ? 0.35 : 0.1);
    state.vuPeakL = state.vuNeedleL >= state.vuPeakL ? state.vuNeedleL : Math.max(0, state.vuPeakL - 0.008);
    state.vuPeakR = state.vuNeedleR >= state.vuPeakR ? state.vuNeedleR : Math.max(0, state.vuPeakR - 0.008);

    if (state.stage === 'vu') {
      vuCtx.clearRect(0, 0, vuCanvas.width, vuCanvas.height);
      drawSingleVU(vuCtx, 0, 0, 478, 540, state.vuNeedleL, state.vuPeakL, 'LEFT', tc);
      drawSingleVU(vuCtx, 498, 0, 478, 540, state.vuNeedleR, state.vuPeakR, 'RIGHT', tc);
    } else if (state.stage === 'spectrum') {
      const { fft, wave } = audio.getSpectrumAndWave();
      drawSpectrumStage(tc, fft, wave);
    }

    if (state.tab === 'play' && !state.inSettings) drawWaveform();
    requestAnimationFrame(renderLoop);
  }

  /* ------------------------------------------------------------------------
     FULL NINTENDO DS KEYPAD ENGINE (100% KEYPAD OPERABLE EVERYWHERE!)
     ------------------------------------------------------------------------ */
  function handleSheetKeypad(sheetType, btn) {
    if (sheetType === 'info') {
      const body = $c('#infoScrollBody');
      if (btn === 'B' || btn === 'A') { closeAllSheets(true); return; }
      if (btn === 'DOWN') { sfx('tick'); smoothScrollTo(body, body.scrollTop + 160, 180); return; }
      if (btn === 'UP') { sfx('tick'); smoothScrollTo(body, body.scrollTop - 160, 180); return; }
      return;
    }

    const items =
      sheetType === 'sort'
        ? getSortSheetAllButtons()
        : Array.from(
            (sheetType === 'action' ? $c('#actionSheetButtons')
              : sheetType === 'output' ? $c('#outputDeviceList')
                : $c('#pickerPlaylistList')).children
          );
    if (!items.length) return;

    const cols = (sheetType === 'picker' || sheetType === 'output') ? 1 : 2;
    let idx = state.sheetFocusIdx;

    if (btn === 'B') { closeAllSheets(true); return; }
    if (btn === 'A') { items[idx]?.click(); return; }
    if (btn === 'RIGHT' && cols === 2) idx = Math.min(items.length - 1, idx + 1);
    else if (btn === 'LEFT' && cols === 2) idx = Math.max(0, idx - 1);
    else if (btn === 'DOWN') idx = Math.min(items.length - 1, idx + cols);
    else if (btn === 'UP') idx = Math.max(0, idx - cols);
    else return;

    if (idx !== state.sheetFocusIdx) {
      state.sheetFocusIdx = idx;
      sfx('tick');
      items.forEach((el, i) => el.classList.toggle('kp-focus', i === idx));
      items[idx]?.scrollIntoView({ block: 'nearest' });
    } else {
      sfx('bump');
    }
  }

  function handleSettingsKeypad(btn) {
    if (btn === 'B') { closeSettings(); return; }
    if (btn === 'UP') {
      if (state.settingsFocusRow > 0) { state.settingsFocusRow--; sfx('tick'); syncSettingsFocusUI(); }
      else sfx('bump');
      return;
    }
    if (btn === 'DOWN') {
      if (state.settingsFocusRow < 3) { state.settingsFocusRow++; sfx('tick'); syncSettingsFocusUI(); }
      else sfx('bump');
      return;
    }
    if (btn === 'LEFT' || btn === 'RIGHT' || btn === 'A') {
      const dir = btn === 'LEFT' ? -1 : 1;
      if (state.settingsFocusRow === 0) {
        setBrightnessIndex(state.brightnessIdx + dir);
      } else if (state.settingsFocusRow === 1) {
        const list = ['obsidian', 'ceramic', 'vfd'];
        setTheme(list[(list.indexOf(state.theme) + dir + list.length) % list.length]);
      } else if (state.settingsFocusRow === 2) {
        const next = state.lidMode === 'pocket' ? 'pause' : 'pocket';
        state.lidMode = next;
        $$c('[data-lid-mode]').forEach((x) => x.classList.toggle('active', x.dataset.lidMode === next));
        sfx('tick'); savePrefs(); showToast(`Lid Close: ${next.toUpperCase()}`);
      } else if (state.settingsFocusRow === 3) {
        state.sfxEnabled = !state.sfxEnabled;
        $$c('[data-sfx]').forEach((x) => x.classList.toggle('active', (x.dataset.sfx === 'on') === state.sfxEnabled));
        savePrefs(); if (state.sfxEnabled) sfx('confirm'); showToast(`UI Sound FX: ${state.sfxEnabled ? 'ON' : 'MUTED'}`);
      }
    }
  }

  function isPlayDirectMode() {
    return state.tab === 'play' && !state.inSettings && !activeSheetType();
  }

  let playHoldBtn = null;
  let playHoldTimer = null;
  let playHoldInterval = null;
  let playHoldSeeking = false;
  let playHoldTicks = 0;

  function startPlayDirHold(btn) {
    if (playHoldBtn === btn) return;
    if (playHoldBtn) endPlayDirHold(playHoldBtn, true);
    playHoldBtn = btn;
    playHoldSeeking = false;
    playHoldTicks = 0;

    const shellBtn = document.querySelector(`[data-hw="${btn}"]`);
    if (shellBtn) shellBtn.classList.add('pressed');

    const dir = btn === 'LEFT' ? -1 : 1;
    playHoldTimer = setTimeout(() => {
      if (!state.currentTrackId || audio.duration <= 0) return;
      playHoldSeeking = true;
      const stepSeek = () => {
        playHoldTicks++;
        const sec = playHoldTicks > 10 ? 5 : 2.5;
        audio.seek(audio.currentTime + dir * sec);
        showToast(`${dir < 0 ? '⏪' : '⏩'} ${fmtTime(audio.currentTime)} / ${fmtTime(audio.duration)}`);
      };
      stepSeek();
      playHoldInterval = setInterval(stepSeek, 120);
    }, 320);
  }

  function endPlayDirHold(btn, cancelled = false) {
    if (playHoldBtn !== btn) return;
    clearTimeout(playHoldTimer);
    clearInterval(playHoldInterval);
    playHoldTimer = null;
    playHoldInterval = null;

    const shellBtn = document.querySelector(`[data-hw="${btn}"]`);
    if (shellBtn) shellBtn.classList.remove('pressed');

    const wasSeeking = playHoldSeeking;
    playHoldBtn = null;
    playHoldSeeking = false;
    playHoldTicks = 0;

    if (!cancelled && !wasSeeking) {
      if (btn === 'LEFT') prevTrack();
      else nextTrack(false);
    }
  }

  function handlePlayKeypad(btn) {
    // Direct hardware control on PLAY — zero cursor clutter!
    if (btn === 'A') { togglePlayWithSfx(); return; }
    if (btn === 'Y') { openTrackContextMenu({ trackId: state.currentTrackId, source: 'play' }); return; }
    if (btn === 'SELECT') { openSettings(); return; }
    if (btn === 'UP') { adjustVolume(5); return; }
    if (btn === 'DOWN') { adjustVolume(-5); return; }
    if (btn === 'LEFT') { prevTrack(); return; }
    if (btn === 'RIGHT') { nextTrack(false); return; }
  }

  function dispatchHardware(btn) {
    const shellBtn = document.querySelector(`[data-hw="${btn}"]`);
    if (shellBtn) {
      shellBtn.classList.add('pressed');
      setTimeout(() => shellBtn.classList.remove('pressed'), 130);
    }

    // Global Hardware Shortcuts
    if (btn === 'START') {
      if (state.oskOpen && !state.lidClosed) triggerOskKey({ act: 'done' });
      else togglePlayWithSfx();
      return;
    }
    if (btn === 'AUX1') { toggleStarTrack(state.currentTrackId); return; }
    if (btn === 'AUX2') { handleAuxNowJump(); return; }
    if (btn === 'X') { cycleStage(); return; }
    if (btn === 'L2') { prevTrack(); return; }
    if (btn === 'R2') { nextTrack(false); return; }

    // Bounded L1 / R1 Tab Switching (Left Stop = PLAY, Right Stop = QUEUE — NO WRAP!)
    if (btn === 'L1') {
      closeAllSheets(false);
      if (state.inSettings) { closeSettings(); return; }
      const curIdx = TOP_TABS.indexOf(state.tab);
      if (curIdx > 0) setTab(TOP_TABS[curIdx - 1]);
      else sfx('bump');
      return;
    }
    if (btn === 'R1') {
      closeAllSheets(false);
      const curIdx = TOP_TABS.indexOf(state.tab);
      if (curIdx < TOP_TABS.length - 1) setTab(TOP_TABS[curIdx + 1]);
      else sfx('bump');
      return;
    }

    // 0) Active On-Screen Virtual Keypad captures D-Pad / A / B / Y cleanly
    if (state.oskOpen) {
      handleOskKeypad(btn);
      return;
    }

    // 1) Active Modal Sheet captures D-Pad / A / B cleanly
    const sheet = activeSheetType();
    if (sheet) {
      handleSheetKeypad(sheet, btn);
      return;
    }

    // 2) Settings Screen
    if (state.inSettings) {
      handleSettingsKeypad(btn);
      return;
    }

    // 3) Play Deck
    if (state.tab === 'play') {
      handlePlayKeypad(btn);
      return;
    }

    // 4) Queue Tab (1-column list: LEFT/RIGHT = Page Up / Page Down)
    if (state.tab === 'queue') {
      const n = state.queue.length;
      if (!n) return;
      const qList = $c('#queueList');
      const pageStep = Math.max(4, Math.floor((qList.clientHeight || 600) / 120));
      if (btn === 'DOWN' || btn === 'RIGHT') {
        const isPage = btn === 'RIGHT';
        const step = isPage ? pageStep : 1;
        const next = Math.min(n - 1, state.queueFocusIdx + step);
        if (next !== state.queueFocusIdx) {
          state.queueFocusIdx = next;
          sfx('tick');
          renderQueue(isPage ? 250 : 155);
        } else sfx('bump');
      } else if (btn === 'UP' || btn === 'LEFT') {
        const isPage = btn === 'LEFT';
        const step = isPage ? pageStep : 1;
        const next = Math.max(0, state.queueFocusIdx - step);
        if (next !== state.queueFocusIdx) {
          state.queueFocusIdx = next;
          sfx('tick');
          renderQueue(isPage ? 250 : 155);
        } else sfx('bump');
      } else if (btn === 'A') {
        sfx('confirm');
        playFromQueue(state.queueFocusIdx);
      } else if (btn === 'Y') {
        openTrackContextMenu({ trackId: state.queue[state.queueFocusIdx], source: 'queue', queueIdx: state.queueFocusIdx });
      }
      return;
    }

    // 5) Catalog Detail Drill-Down (Album / Artist / Playlist Detail)
    const detail = state.detailStack[state.detailStack.length - 1];
    if (detail) {
      const albums = detail.type === 'artist' ? state.activeDetailAlbums : [];
      const albCount = albums.length;
      const tracks = state.activeDetailTracks;
      const totalItems = albCount + tracks.length;
      if (btn === 'B') { popDetail(); return; }

      if (btn === 'DOWN') {
        if (!totalItems) { sfx('bump'); return; }
        let next = state.detailFocusIdx;
        if (state.detailFocusIdx < 0) {
          next = 0;
        } else if (state.detailFocusIdx < albCount) {
          // Inside 2-column Artist Albums grid
          if (state.detailFocusIdx + 2 < albCount) next = state.detailFocusIdx + 2;
          else if (Math.floor(state.detailFocusIdx / 2) < Math.floor((albCount - 1) / 2)) next = albCount - 1;
          else if (tracks.length > 0) next = albCount;
        } else {
          next = Math.min(totalItems - 1, state.detailFocusIdx + 1);
        }
        if (next !== state.detailFocusIdx) { state.detailFocusIdx = next; sfx('tick'); renderCatalog(); }
        else sfx('bump');
      } else if (btn === 'UP') {
        if (state.detailFocusIdx < 0) {
          sfx('bump');
        } else if (state.detailFocusIdx < albCount) {
          // Inside 2-column Artist Albums grid
          state.detailFocusIdx = state.detailFocusIdx >= 2 ? state.detailFocusIdx - 2 : -2;
          sfx('tick');
          renderCatalog();
        } else if (state.detailFocusIdx === albCount) {
          // Top of Songs section -> jump up to last Album (or Header Play if no albums)
          state.detailFocusIdx = albCount > 0 ? albCount - 1 : -2;
          sfx('tick');
          renderCatalog();
        } else {
          state.detailFocusIdx--;
          sfx('tick');
          renderCatalog();
        }
      } else if (btn === 'RIGHT') {
        if (state.detailFocusIdx < 0) {
          if (state.detailFocusIdx < -1) { state.detailFocusIdx++; sfx('tick'); renderCatalog(); }
          else sfx('bump');
        } else if (state.detailFocusIdx < albCount) {
          if (state.detailFocusIdx % 2 === 0 && state.detailFocusIdx + 1 < albCount) {
            state.detailFocusIdx++;
            sfx('tick');
            renderCatalog();
          } else sfx('bump');
        } else {
          // 1-column Detail track list: RIGHT = Page Down
          const pageStep = Math.max(4, Math.floor((libItemsEl.clientHeight || 540) / (detail.type === 'album' ? 104 : 128)));
          const next = Math.min(totalItems - 1, state.detailFocusIdx + pageStep);
          if (next !== state.detailFocusIdx) { state.detailFocusIdx = next; sfx('tick'); renderCatalog(250); }
          else sfx('bump');
        }
      } else if (btn === 'LEFT') {
        if (state.detailFocusIdx < 0) {
          if (state.detailFocusIdx > -3) { state.detailFocusIdx--; sfx('tick'); renderCatalog(); }
          else sfx('bump');
        } else if (state.detailFocusIdx < albCount) {
          if (state.detailFocusIdx % 2 === 1) {
            state.detailFocusIdx--;
            sfx('tick');
            renderCatalog();
          } else sfx('bump');
        } else {
          // 1-column Detail track list: LEFT = Page Up
          const pageStep = Math.max(4, Math.floor((libItemsEl.clientHeight || 540) / (detail.type === 'album' ? 104 : 128)));
          const next = Math.max(albCount, state.detailFocusIdx - pageStep);
          if (next !== state.detailFocusIdx) { state.detailFocusIdx = next; sfx('tick'); renderCatalog(250); }
          else sfx('bump');
        }
      } else if (btn === 'A') {
        if (state.detailFocusIdx === -3) $c('#btnDetailShufflePlay').click();
        else if (state.detailFocusIdx === -2) $c('#btnDetailPlayAll').click();
        else if (state.detailFocusIdx === -1) $c('#btnDetailQueueAll').click();
        else if (state.detailFocusIdx < albCount && albums[state.detailFocusIdx]) {
          goToAlbum(albums[state.detailFocusIdx].id);
        } else {
          const tIdx = state.detailFocusIdx - albCount;
          if (tracks[tIdx]) {
            sfx('confirm');
            selectTrack(tracks[tIdx].id, true, tracks.map((t) => t.id));
          }
        }
      } else if (btn === 'Y' && state.detailFocusIdx >= albCount) {
        const tIdx = state.detailFocusIdx - albCount;
        if (tracks[tIdx]) {
          openTrackContextMenu({
            trackId: tracks[tIdx].id,
            source: detail.type === 'playlist' ? 'playlist' : 'catalog',
            playlistId: detail.type === 'playlist' ? detail.id : null,
          });
        }
      }
      return;
    }

    // 6) Root Catalog Views (with -1 Pull/Press-Up Utility Bar!)
    const items = state.activeSortedItems;
    if (btn === 'SELECT') {
      if (state.tab === 'playlists') openPlaylistPicker(state.currentTrackId);
      else openSortOrderSheet();
      return;
    }

    if (btn === 'B' && isSearchBarActive()) {
      closeCatalogSearch(true);
      return;
    }

    // If focus is currently inside the -1 Utility Bar
    if (state.inMinusOne) {
      const controls = getMinusOneControls();
      if (btn === 'DOWN' || btn === 'B') {
        hideMinusOneBar(true);
        return;
      }
      if (btn === 'UP') { sfx('bump'); return; }
      if (btn === 'LEFT') {
        if (state.minusOneFocusIdx > 0) {
          state.minusOneFocusIdx--;
          sfx('tick');
          syncMinusOneBarUI();
        } else sfx('bump');
        return;
      }
      if (btn === 'RIGHT') {
        if (state.minusOneFocusIdx < controls.length - 1) {
          state.minusOneFocusIdx++;
          sfx('tick');
          syncMinusOneBarUI();
        } else sfx('bump');
        return;
      }
      if (btn === 'A') {
        controls[state.minusOneFocusIdx]?.click();
        return;
      }
      return;
    }

    if (!items.length) {
      if (btn === 'UP') revealMinusOneBar(true);
      return;
    }

    const cols = state.tab === 'albums' ? getAlbumsLayout().cols : 1;
    let nextIdx = state.listFocusIdx;

    // Pressing UP from top row reveals and focuses the -1 Utility Bar!
    if (btn === 'UP' && state.listFocusIdx < cols) {
      revealMinusOneBar(true);
      return;
    }

    if (btn === 'DOWN') nextIdx = Math.min(items.length - 1, nextIdx + cols);
    else if (btn === 'UP') nextIdx = Math.max(0, nextIdx - cols);
    else if (btn === 'RIGHT') {
      if (cols >= 2) {
        nextIdx = Math.min(items.length - 1, nextIdx + 1);
      } else {
        // 1-column list view: RIGHT = Page Down
        const rowH = state.tab === 'albums' ? 130 : state.tab === 'tracks' ? 128 : state.tab === 'artists' ? 108 : 124;
        const pageStep = Math.max(4, Math.floor((libItemsEl.clientHeight || 580) / rowH));
        nextIdx = Math.min(items.length - 1, nextIdx + pageStep);
      }
    } else if (btn === 'LEFT') {
      if (cols >= 2) {
        nextIdx = Math.max(0, nextIdx - 1);
      } else {
        // 1-column list view: LEFT = Page Up
        const rowH = state.tab === 'albums' ? 130 : state.tab === 'tracks' ? 128 : state.tab === 'artists' ? 108 : 124;
        const pageStep = Math.max(4, Math.floor((libItemsEl.clientHeight || 580) / rowH));
        nextIdx = Math.max(0, nextIdx - pageStep);
      }
    }
    else if (btn === 'A') {
      const it = items[state.listFocusIdx];
      if (state.tab === 'albums') goToAlbum(it.id);
      else if (state.tab === 'artists') goToArtist(it.name);
      else if (state.tab === 'playlists') pushDetail({ type: 'playlist', id: it.id });
      else if (state.tab === 'tracks') { sfx('confirm'); selectTrack(it.id, true); }
      return;
    } else if (btn === 'Y' && state.tab === 'tracks') {
      const trk = items[state.listFocusIdx];
      if (trk) openTrackContextMenu({ trackId: trk.id, source: 'catalog' });
      return;
    } else {
      return;
    }

    if (nextIdx !== state.listFocusIdx) {
      const isPageJump = cols === 1 && (btn === 'LEFT' || btn === 'RIGHT');
      state.listFocusIdx = nextIdx;
      if (state.minusOneOpen && nextIdx >= cols) hideMinusOneBar(false);
      sfx('tick');
      if (state.tab === 'playlists') {
        renderCatalog();
        smoothScrollIntoView(libItemsEl, libItemsEl.children[nextIdx], isPageJump ? 250 : 155);
      } else {
        scrollToFocusedCatalogItem(isPageJump);
      }
    } else {
      sfx('bump');
    }
  }

  let volPopupTimer = null;
  let volumePointer = null;

  function scheduleVolumePopupHide() {
    clearTimeout(volPopupTimer);
    if (volumePointer === null) {
      volPopupTimer = setTimeout(() => $c('#volPopup').classList.remove('show'), 700);
    }
  }

  function showVolumePopup(effective, isMuted) {
    const pop = $c('#volPopup');
    const norm = Math.max(0, Math.min(1, effective / 100));
    pop.textContent = String(effective);
    pop.classList.toggle('muted', isMuted);
    pop.style.left = `calc(22px + (100% - 44px) * ${norm.toFixed(4)})`;
    pop.classList.add('show');
    scheduleVolumePopupHide();
  }

  function adjustVolume(deltaPct) {
    const base = state.muted ? 0 : state.volume;
    const next = Math.max(0, Math.min(100, base + deltaPct));
    if (next === base && !state.muted) { sfx('bump'); showVolumePopup(next, next === 0); return; }
    state.muted = next === 0;
    sfx('tick');
    updateVolumeUI(next, true, true);
  }

  function toggleMute() {
    if (state.muted || state.volume === 0) {
      state.muted = false;
      const restored = state.lastNonZeroVol > 0 ? state.lastNonZeroVol : 76;
      sfx('tick');
      updateVolumeUI(restored, true, true);
      showToast(`Volume ${restored}`);
    } else {
      state.lastNonZeroVol = state.volume;
      state.muted = true;
      sfx('dim');
      updateVolumeUI(state.volume, true, true);
      showToast('Muted');
    }
  }

  function updateVolumeUI(val, persist = true, popup = false) {
    state.volume = val;
    if (val > 0) state.lastNonZeroVol = val;
    const isMuted = state.muted || val === 0;
    const effective = isMuted ? 0 : val;
    audio.setVolume(effective / 100);
    $c('#volumeSlider').value = effective;
    $c('#volCard').classList.toggle('muted', isMuted);
    $c('#btnMuteToggle').classList.toggle('muted', isMuted);
    $c('#muteBtnIcon').setAttribute('href', isMuted ? '#ic-speaker-mute' : '#ic-speaker');
    if (popup) showVolumePopup(effective, isMuted);
    if (persist) savePrefs();
  }

  /* ------------------------------------------------------------------------
     WIRE ALL EVENTS
     ------------------------------------------------------------------------ */
  document.querySelectorAll('[data-set-view]').forEach((b) => b.addEventListener('click', () => setView(b.dataset.setView)));
  document.querySelectorAll('[data-set-theme]').forEach((b) => b.addEventListener('click', () => setTheme(b.dataset.setTheme)));
  document.getElementById('btnIntegerScale').addEventListener('click', toggleIntegerScale);
  document.getElementById('btnToggle2k').addEventListener('click', toggle2kSim);
  document.getElementById('btnLidToggle').addEventListener('click', () => toggleLid());
  document.getElementById('btnOpenLid').addEventListener('click', () => toggleLid(false));
  document.getElementById('btnHelpToggle').addEventListener('click', () => { sfx('tick'); document.getElementById('helpModal').hidden = false; });
  document.getElementById('btnHelpClose').addEventListener('click', () => { sfx('back'); document.getElementById('helpModal').hidden = true; });
  document.querySelectorAll('[data-hw]').forEach((btn) => {
    const hw = btn.dataset.hw;
    if (hw === 'LEFT' || hw === 'RIGHT') {
      btn.addEventListener('pointerdown', (e) => {
        if (isPlayDirectMode()) {
          btn.setPointerCapture(e.pointerId);
          startPlayDirHold(hw);
        } else {
          dispatchHardware(hw);
        }
      });
      btn.addEventListener('pointerup', () => endPlayDirHold(hw, false));
      btn.addEventListener('pointercancel', () => endPlayDirHold(hw, true));
    } else {
      btn.addEventListener('click', () => dispatchHardware(hw));
    }
  });

  // Unified Tactile Touch / Stylus Press Feedback on Bottom 4.3" Screen
  const PRESSABLE_SEL = [
    '.tactile-btn', '.quick-pad', '.wf-icon-btn', '.vol-mute-btn', '.output-menu-btn',
    '.deck-tab', '.nav-shoulder-hint', '.sort-pill-btn', '.view-mode-btn',
    '.cat-act-btn', '.detail-back-btn', '.detail-act-btn', '.track-q-btn',
    '.q-mini-btn', '.seg-btn', '.sheet-big-btn', '.sheet-close', '.osk-key', '.cat-search-close',
    '.track-row', '.album-grid-tile', '.album-list-row', '.album-card',
    '.artist-row', '.playlist-row', '.queue-row', '.picker-row',
  ].join(',');

  function getTwoToneVoiceForEl(el) {
    if (!el) return null;
    const id = el.id || '';
    const hw = el.dataset?.hw || '';
    if (el.matches('.deck-tab, .nav-shoulder-hint') || hw === 'L1' || hw === 'R1') return 'deck';
    if (id === 'btnPlayPause' || hw === 'START') return audio.isPlaying ? 'pause' : 'play';
    if (id === 'btnDetailPlayAll' || id === 'btnDetailShufflePlay' || id === 'btnCatPlayAll' || id === 'btnCatShufflePlay') return 'play';
    if (id === 'btnDetailQueueAll' || el.dataset?.act === 'playnext') return 'add';
    if (id === 'btnCycleStage' || hw === 'X') return 'stage';
    if (id === 'btnDetailBack' || id === 'btnSettingsBack' || el.matches('.sheet-close, .danger-btn') || hw === 'B') return 'back';
    if (el.matches('.track-row, .album-grid-tile, .album-list-row, .album-card, .artist-row, .playlist-row, .queue-row, .picker-row') || hw === 'A') return 'confirm';
    return null;
  }

  let activePressedEl = null;
  let activePressVoice = null;
  let activePressStartTs = 0;

  function releasePressedVisual(playReleaseSfx = true) {
    if (!activePressedEl) return;
    const el = activePressedEl;
    const voice = activePressVoice;
    activePressedEl = null;
    activePressVoice = null;
    if (playReleaseSfx) {
      if (voice) {
        audio.playSfx && state.sfxEnabled && audio.playSfx(`${voice}:up`);
        splitHandledVoice = voice;
        splitHandledTs = performance.now();
      } else {
        if (state.sfxEnabled) audio.playSfx('release');
        splitHandledVoice = 'press';
        splitHandledTs = performance.now();
      }
    }
    const elapsed = performance.now() - activePressStartTs;
    const holdLeft = Math.max(0, 110 - elapsed);
    if (holdLeft > 0) {
      setTimeout(() => el.classList.remove('is-pressed'), holdLeft);
    } else {
      el.classList.remove('is-pressed');
    }
  }

  consoleRoot.addEventListener('pointerdown', (e) => {
    // Depressed visual cue + split two-tone (1st tone on press, 2nd tone on release!)
    const target = e.target.closest(PRESSABLE_SEL);
    if (target) {
      if (activePressedEl && activePressedEl !== target) activePressedEl.classList.remove('is-pressed');
      activePressedEl = target;
      activePressVoice = getTwoToneVoiceForEl(target);
      activePressStartTs = performance.now();
      target.classList.add('is-pressed');
      if (state.sfxEnabled) audio.playSfx(activePressVoice ? `${activePressVoice}:down` : 'press');
    }
  });
  window.addEventListener('pointerup', () => releasePressedVisual(true));
  window.addEventListener('pointercancel', () => releasePressedVisual(false));

  // Split two-tone press/release sound for physical RG DS shell hardware buttons
  document.querySelectorAll('[data-hw]').forEach((btn) => {
    if (consoleRoot.contains(btn)) return;
    let hwVoice = null;
    btn.addEventListener('pointerdown', () => {
      hwVoice = getTwoToneVoiceForEl(btn);
      if (state.sfxEnabled) audio.playSfx(hwVoice ? `${hwVoice}:down` : 'press');
    });
    btn.addEventListener('pointerup', () => {
      if (hwVoice) {
        if (state.sfxEnabled) audio.playSfx(`${hwVoice}:up`);
        splitHandledVoice = hwVoice;
        splitHandledTs = performance.now();
      } else {
        if (state.sfxEnabled) audio.playSfx('release');
        splitHandledVoice = 'press';
        splitHandledTs = performance.now();
      }
      hwVoice = null;
    });
    btn.addEventListener('pointercancel', () => { hwVoice = null; });
  });

  // Single Analog Stick
  const stickWell = document.getElementById('analogStickWell');
  const stickCap = document.getElementById('analogStickCap');
  let stickDragging = false;
  stickWell.addEventListener('pointerdown', (e) => { stickDragging = true; stickWell.setPointerCapture(e.pointerId); });
  stickWell.addEventListener('pointermove', (e) => {
    if (!stickDragging) return;
    const rect = stickWell.getBoundingClientRect();
    const dx = Math.max(-22, Math.min(22, e.clientX - (rect.left + rect.width / 2)));
    const dy = Math.max(-22, Math.min(22, e.clientY - (rect.top + rect.height / 2)));
    stickCap.style.transform = `translate(${dx}px, ${dy}px)`;
    if (state.tab === 'play' && !state.inSettings && state.currentTrackId && audio.duration > 0 && Math.abs(dx) > 6) {
      audio.seek(audio.currentTime + (dx / 22) * 1.4);
    }
  });
  const resetStick = () => { stickDragging = false; stickCap.style.transform = 'translate(0, 0)'; };
  stickWell.addEventListener('pointerup', resetStick);
  stickWell.addEventListener('pointercancel', resetStick);

  // Top Expanding Tabs (tapping active tab pops detail back to root!)
  $$c('.deck-tab').forEach((t) => t.addEventListener('click', () => {
    if (t.dataset.deck === state.tab && !state.inSettings && state.detailStack.length) {
      sfx('back');
      state.detailStack = [];
      renderCatalog();
    } else {
      setTab(t.dataset.deck);
    }
  }));

  // Play Controls (Prev/Next also support Tap = Track Skip, Long-Press = Seek!)
  $c('#btnPlayPause').addEventListener('click', togglePlayWithSfx);
  const bindTransportSkipHold = (el, hw) => {
    el.addEventListener('pointerdown', (e) => {
      el.setPointerCapture(e.pointerId);
      startPlayDirHold(hw);
    });
    el.addEventListener('pointerup', () => endPlayDirHold(hw, false));
    el.addEventListener('pointercancel', () => endPlayDirHold(hw, true));
  };
  bindTransportSkipHold($c('#btnPrev'), 'LEFT');
  bindTransportSkipHold($c('#btnNext'), 'RIGHT');
  $c('#btnStar').addEventListener('click', () => toggleStarTrack(state.currentTrackId));
  $c('#btnNowContext').addEventListener('click', () => openTrackContextMenu({ trackId: state.currentTrackId, source: 'play' }));
  $c('#btnOpenSettings').addEventListener('click', openSettings);
  $c('#btnOpenOutputSheet').addEventListener('click', openOutputSheet);
  $c('#btnCloseOutputSheet').addEventListener('click', () => closeAllSheets(true));
  $c('#btnSettingsBack').addEventListener('click', closeSettings);
  $c('#btnCycleStage').addEventListener('click', cycleStage);
  $c('#btnGoToCurrentAlbum').addEventListener('click', () => {
    const trk = getCurrentTrack();
    if (trk) goToAlbum(trk.albumId);
    else sfx('bump');
  });
  $c('#btnShuffle').addEventListener('click', () => {
    sfx('tick');
    state.shuffle = !state.shuffle;
    syncQuickPadsUI();
    savePrefs();
    showToast(state.shuffle ? 'Shuffle ON' : 'Shuffle OFF');
  });
  $c('#btnRepeat').addEventListener('click', () => {
    sfx('tick');
    const order = ['all', 'one', 'off'];
    state.repeat = order[(order.indexOf(state.repeat) + 1) % order.length];
    syncQuickPadsUI();
    savePrefs();
    showToast(`Repeat: ${state.repeat.toUpperCase()}`);
  });

  // Catalog Back, Search, Play/Shuffle, Album View Toggle, Sort Order & New Playlist
  $c('#btnDetailBack').addEventListener('click', popDetail);
  $c('#btnCatSearchClose').addEventListener('click', () => closeCatalogSearch(true));
  $c('#catSearchBar').addEventListener('click', (e) => {
    if (e.target.closest('#btnCatSearchClose')) return;
    if (!state.oskOpen || state.oskTargetId !== 'catSearchInput') openCatalogSearch();
    else $c('#catSearchInput').focus();
  });
  $c('#catSearchInput').addEventListener('pointerdown', () => {
    if (!state.oskOpen || state.oskTargetId !== 'catSearchInput') openCatalogSearch();
  });
  $c('#catSearchInput').addEventListener('input', (e) => {
    state.searchQuery = e.target.value;
    state.listFocusIdx = 0;
    libItemsEl.scrollTop = 0;
    syncMinusOneBarUI();
    renderCatalog();
  });
  $c('#newPlaylistInput').addEventListener('pointerdown', () => {
    if (!state.oskOpen || state.oskTargetId !== 'newPlaylistInput') openOsk('newPlaylistInput', true);
  });
  $c('#btnCatShufflePlay').addEventListener('click', () => playCatalogRoot(true));
  $c('#btnCatPlayAll').addEventListener('click', () => playCatalogRoot(false));
  $c('#btnAlbumViewGrid').addEventListener('click', () => setAlbumView('grid'));
  $c('#btnAlbumViewList').addEventListener('click', () => setAlbumView('list'));
  $c('#btnCatNewPlaylist').addEventListener('click', () => openPlaylistPicker(state.currentTrackId));
  $c('#btnOpenSortSheet').addEventListener('click', openSortOrderSheet);

  // Sheets Close & Playlist Create
  $c('#btnCloseActionSheet').addEventListener('click', () => closeAllSheets(true));
  $c('#btnCloseSortSheet').addEventListener('click', () => closeAllSheets(true));
  $c('#btnClosePickerSheet').addEventListener('click', () => closeAllSheets(true));
  $c('#btnCloseInfoSheet').addEventListener('click', () => closeAllSheets(true));
  $c('#btnCreateAndAddPl').addEventListener('click', () => {
    const input = $c('#newPlaylistInput');
    const name = input.value.trim();
    if (!name) return;
    const newPl = {
      id: `pl-${Date.now()}`,
      name,
      trackIds: state.pickerTrackId ? [state.pickerTrackId] : [],
    };
    state.playlists.push(newPl);
    input.value = '';
    sfx('star');
    savePrefs();
    showToast(`Created Playlist: ${name}`);
    renderPlaylistPickerList();
    renderActiveView();
  });

  // Volume, Mute Toggle, Brightness Notched Slider & Settings
  $c('#btnMuteToggle').addEventListener('click', toggleMute);
  $c('#volumeSlider').addEventListener('input', (e) => {
    const val = Number(e.target.value);
    state.muted = val === 0;
    updateVolumeUI(val, true, true);
  });
  $c('#volumeSlider').addEventListener('pointerdown', (e) => {
    if (e.button !== 0) return;
    volumePointer = e.pointerId;
    const isMuted = state.muted || state.volume === 0;
    showVolumePopup(isMuted ? 0 : state.volume, isMuted);
  });
  function endVolumeDrag(e) {
    if (volumePointer !== e.pointerId) return;
    volumePointer = null;
    scheduleVolumePopupHide();
  }
  window.addEventListener('pointerup', endVolumeDrag);
  window.addEventListener('pointercancel', endVolumeDrag);
  $c('#brightnessSlider').addEventListener('input', (e) => setBrightnessIndex(Number(e.target.value)));

  $$c('[data-lid-mode]').forEach((b) => {
    b.addEventListener('click', () => {
      sfx('tick');
      state.lidMode = b.dataset.lidMode;
      $$c('[data-lid-mode]').forEach((x) => x.classList.toggle('active', x === b));
      savePrefs();
      showToast(`Lid Close: ${state.lidMode.toUpperCase()}`);
    });
  });
  $$c('[data-sfx]').forEach((b) => {
    b.addEventListener('click', () => {
      state.sfxEnabled = b.dataset.sfx === 'on';
      $$c('[data-sfx]').forEach((x) => x.classList.toggle('active', x === b));
      savePrefs();
      if (state.sfxEnabled) sfx('confirm');
      showToast(`UI Sound FX: ${state.sfxEnabled ? 'ON' : 'MUTED'}`);
    });
  });

  // Local Audio Importer
  const fileInput = document.getElementById('audioFileInput');
  function importAudioFiles(files) {
    let firstAdded = null;
    const today = new Date().toISOString().slice(0, 10);
    Array.from(files).forEach((file, idx) => {
      if (!file.type.startsWith('audio/')) return;
      const ext = (file.name.split('.').pop() || 'AUDIO').toUpperCase();
      const trk = {
        id: `local-${Date.now()}-${idx}`,
        albumId: 'alb-local-import',
        trackNo: idx + 1,
        title: file.name.replace(/\.[^.]+$/, ''),
        artist: 'Local MicroSD',
        album: 'Imported Audio',
        year: 2026,
        addedAt: today,
        format: ext,
        sampleRate: '48.0 kHz',
        bitrate: `${Math.round(file.size / 1024)} KB`,
        hires: ext === 'FLAC' || ext === 'WAV',
        starred: false,
        duration: 180,
        artStyle: 'bauhaus-amber',
        colors: ['#ff7a1a', '#1b1f2a', '#f2efe9'],
        localUrl: URL.createObjectURL(file),
        waveform: generateWaveform(Math.random() * 20),
      };
      state.tracks.unshift(trk);
      if (!firstAdded) firstAdded = trk;
      else state.queue.unshift(trk.id);
    });
    if (firstAdded) {
      rebuildIndexes();
      sfx('add');
      selectTrack(firstAdded.id, true);
      showToast(`Loaded: ${firstAdded.title}`);
    }
  }
  fileInput.addEventListener('change', (e) => importAudioFiles(e.target.files));
  document.addEventListener('dragover', (e) => e.preventDefault());
  document.addEventListener('drop', (e) => {
    e.preventDefault();
    if (e.dataTransfer?.files?.length) importAudioFiles(e.dataTransfer.files);
  });

  const keyboardButtons = {
    ' ': 'START', tab: 'SELECT', c: 'X', q: 'L1', e: 'R1',
    '1': 'L2', '2': 'R2', z: 'A', enter: 'A', x: 'B', backspace: 'B',
    v: 'Y', d: 'AUX1', h: 'AUX2',
  };
  const keyboardDirections = { ArrowUp: 'UP', ArrowDown: 'DOWN', ArrowLeft: 'LEFT', ArrowRight: 'RIGHT' };
  const heldKeyboardActions = new Map();

  document.addEventListener('keydown', (e) => {
    if (e.isComposing || e.metaKey || e.ctrlKey || e.altKey) return;
    const target = e.target;
    const inText = target.tagName === 'INPUT' && target.type === 'text';
    const direction = keyboardDirections[e.key];
    if (direction) {
      if (inText && !state.oskOpen) return;
      e.preventDefault();
      if (inText) handleOskKeypad(direction);
      else if ((direction === 'LEFT' || direction === 'RIGHT') && isPlayDirectMode()) {
        if (!e.repeat) startPlayDirHold(direction);
      } else {
        dispatchHardware(direction);
      }
      return;
    }

    const key = e.key.toLowerCase();
    let action;
    let shellButton;
    if (e.code === 'Space' && state.oskOpen && !state.lidClosed) {
      const targetId = state.oskTargetId;
      shellButton = document.querySelector('.start-btn');
      action = () => {
        if (state.oskOpen && !state.lidClosed && state.oskTargetId === targetId) dispatchHardware('START');
      };
    } else if (inText) {
      if (target.id === 'catSearchInput' && key === 'escape') {
        action = () => state.oskOpen ? handleOskKeypad('B') : closeCatalogSearch(true);
      } else if (target.id === 'catSearchInput' && key === 'enter') {
        action = () => {
          closeOsk(false);
          target.blur();
          if (!state.searchQuery.trim()) state.searchOpen = false;
          state.inMinusOne = false;
          syncMinusOneBarUI();
          if (activeVirtualRender) activeVirtualRender(true);
          else if (state.tab === 'playlists') renderCatalog();
        };
      } else if (target.id === 'newPlaylistInput' && key === 'escape' && state.oskOpen) {
        action = () => closeOsk(true);
      } else if (target.id === 'newPlaylistInput' && key === 'enter') {
        action = () => {
          closeOsk(false);
          $c('#btnCreateAndAddPl').click();
        };
      }
    } else if (Object.hasOwn(keyboardButtons, key)) {
      const button = keyboardButtons[key];
      shellButton = document.querySelector(`[data-hw="${button}"]`);
      action = () => dispatchHardware(button);
    } else if (key === 'l') {
      action = toggleLid;
    } else if (key === 'escape') {
      action = () => {
        document.getElementById('helpModal').hidden = true;
        closeAllSheets(true);
      };
    } else if (key === '/' && isCatalogTab(state.tab) && !state.detailStack.length && !activeSheetType() && !state.inSettings) {
      action = openCatalogSearch;
    }
    if (action) {
      e.preventDefault();
      if (!e.repeat) {
        shellButton?.classList.add('pressed');
        heldKeyboardActions.set(e.code, { target, shellButton, action });
      }
    }
  });

  document.addEventListener('keyup', (e) => {
    if (e.key === 'ArrowLeft') endPlayDirHold('LEFT', false);
    else if (e.key === 'ArrowRight') endPlayDirHold('RIGHT', false);
    const held = heldKeyboardActions.get(e.code);
    if (held) {
      heldKeyboardActions.delete(e.code);
      held.shellButton?.classList.remove('pressed');
      if (e.target === held.target) held.action();
    }
  });

  window.addEventListener('blur', () => {
    if (playHoldBtn) endPlayDirHold(playHoldBtn, true);
    for (const held of heldKeyboardActions.values()) held.shellButton?.classList.remove('pressed');
    heldKeyboardActions.clear();
    if (volumePointer !== null) {
      volumePointer = null;
      scheduleVolumePopupHide();
    }
  });

  function updateClock() {
    const d = new Date();
    $s('#topClock').textContent = `${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`;
  }
  updateClock();
  setInterval(updateClock, 15000);

  /* ------------------------------------------------------------------------
     INITIAL HYDRATE FROM SAVED PREFS
     ------------------------------------------------------------------------ */
  audio.loadTrack(getCurrentTrack(), false);
  update2kButtonUI();
  setView(state.view, true);
  setTheme(state.theme, true);
  setBrightnessIndex(state.brightnessIdx, true);
  setStage(state.stage, true);
  setTab(state.tab, true);

  $$c('[data-lid-mode]').forEach((x) => x.classList.toggle('active', x.dataset.lidMode === state.lidMode));
  $$c('[data-sfx]').forEach((x) => x.classList.toggle('active', (x.dataset.sfx === 'on') === state.sfxEnabled));

  syncOutputUI();
  updateVolumeUI(state.volume, false);
  syncTrackUI();
  updatePlayStateUI();
  syncQuickPadsUI();
  drawWaveform();
  fitDeviceToViewport();
  requestAnimationFrame(renderLoop);
})();
