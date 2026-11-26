/* ==========================================================================
   DECK DS Bit-Perfect Web Audio Engine (Multi-Track Albums Catalog)
   Procedural Multi-Layered Hi-Fi Demo Synth + Local Audio File Playback
   ========================================================================== */

function generateWaveform(seed, count = 160) {
  const arr = [];
  let v = seed * 13.7;
  for (let i = 0; i < count; i++) {
    const pos = i / count;
    const envelope = Math.sin(pos * Math.PI) * 0.72 + 0.28;
    const wave =
      Math.sin(i * 0.27 + seed) * 0.28 +
      Math.cos(i * 0.61 + seed * 2.1) * 0.22 +
      Math.sin(i * 1.37 + v) * 0.16;
    v = (v * 1.13 + 0.41) % 100;
    arr.push(Math.max(0.12, Math.min(0.98, (0.46 + wave) * envelope)));
  }
  return arr;
}

const DEMO_TRACKS = [
  /* ---- ALBUM 1: Glass Hinge Sessions (Kōji Takahashi, 2026) ---- */
  {
    id: 'trk-1',
    albumId: 'alb-glass-hinge',
    trackNo: 1,
    title: 'Nocturne in 4:3',
    artist: 'Kōji Takahashi',
    album: 'Glass Hinge Sessions',
    year: 2026,
    addedAt: '2026-11-24',
    format: 'FLAC 24-bit',
    sampleRate: '96.0 kHz',
    bitrate: '2,840 kbps',
    hires: true,
    starred: true,
    duration: 216,
    artStyle: 'bauhaus-amber',
    colors: ['#ff7a1a', '#1b2234', '#f2e8d5'],
    synth: { bpm: 92, root: 57, scale: [0, 3, 7, 10, 12, 14, 15], chords: [[0,3,7,10],[5,8,12,15],[3,7,10,14],[7,10,14,17]], wave: 'triangle', feel: 'lofi' },
    waveform: generateWaveform(1.2),
    lyrics: [
      { t: 0, text: 'Instrumental intro — warm Rhodes swell & room tone' },
      { t: 8, text: 'Dual glass horizons glowing softly in the midnight room' },
      { t: 18, text: 'Four-by-three geometry, every pixel crisp and still' },
      { t: 28, text: 'Amber needles climbing gently toward zero decibels' },
      { t: 40, text: 'Close the clamshell hinge and carry the quiet down the avenue' },
      { t: 54, text: 'Lossless rain against the window, ninety-six kilohertz' },
      { t: 74, text: 'Tape reel turning slow beneath a quiet silver moon' },
    ],
  },
  {
    id: 'trk-2',
    albumId: 'alb-glass-hinge',
    trackNo: 2,
    title: 'Stepless Alloy',
    artist: 'Kōji Takahashi',
    album: 'Glass Hinge Sessions',
    year: 2026,
    addedAt: '2026-11-24',
    format: 'FLAC 24-bit',
    sampleRate: '96.0 kHz',
    bitrate: '2,790 kbps',
    hires: true,
    starred: false,
    duration: 194,
    artStyle: 'bauhaus-amber',
    colors: ['#ff7a1a', '#1b2234', '#f2e8d5'],
    synth: { bpm: 96, root: 55, scale: [0, 2, 3, 7, 10, 12, 14], chords: [[0,3,7,10],[7,10,14,17],[5,8,12,15],[2,5,9,12]], wave: 'triangle', feel: 'lofi' },
    waveform: generateWaveform(1.9),
    lyrics: [
      { t: 0, text: 'Warm bass harmonics settling into brushed matte metal' },
      { t: 14, text: 'One hundred fifteen degrees — resting steady on cedar wood' },
      { t: 32, text: 'Soft Rhodes chords echoing across a calm evening studio' },
    ],
  },
  {
    id: 'trk-3',
    albumId: 'alb-glass-hinge',
    trackNo: 3,
    title: 'Afterglow Desk Lamp',
    artist: 'Kōji Takahashi',
    album: 'Glass Hinge Sessions',
    year: 2026,
    addedAt: '2026-11-24',
    format: 'FLAC 24-bit',
    sampleRate: '96.0 kHz',
    bitrate: '2,910 kbps',
    hires: true,
    starred: true,
    duration: 232,
    artStyle: 'bauhaus-amber',
    colors: ['#ff7a1a', '#1b2234', '#f2e8d5'],
    synth: { bpm: 86, root: 53, scale: [0, 2, 4, 7, 9, 12, 14], chords: [[0,4,7,11],[5,9,12,16],[2,5,9,12],[7,11,14,17]], wave: 'sine', feel: 'minimal' },
    waveform: generateWaveform(2.4),
    lyrics: [
      { t: 0, text: 'Late night tungsten glow reflecting softly on the top display' },
      { t: 18, text: 'Slow decay into midnight silence' },
    ],
  },

  /* ---- ALBUM 2: 鴨川カセット紀行 (佐藤ミナ・トリオ, 2025) ---- */
  {
    id: 'trk-4',
    albumId: 'alb-pocket-hifi',
    trackNo: 1,
    title: '京都カセット習作',
    artist: '佐藤ミナ・トリオ',
    album: '鴨川カセット紀行',
    year: 2025,
    addedAt: '2026-10-12',
    format: 'FLAC 24-bit',
    sampleRate: '192.0 kHz',
    bitrate: '4,610 kbps',
    hires: true,
    starred: true,
    duration: 244,
    artStyle: 'vinyl-sage',
    colors: ['#2ad18b', '#132620', '#e9efe9'],
    synth: { bpm: 84, root: 60, scale: [0, 2, 4, 7, 9, 11, 14], chords: [[0,4,7,11],[9,12,16,19],[2,5,9,12],[7,11,14,17]], wave: 'sine', feel: 'jazz' },
    waveform: generateWaveform(3.1),
  },
  {
    id: 'trk-5',
    albumId: 'alb-pocket-hifi',
    trackNo: 2,
    title: '鴨川雨のワルツ',
    artist: '佐藤ミナ・トリオ',
    album: '鴨川カセット紀行',
    year: 2025,
    addedAt: '2026-10-12',
    format: 'FLAC 24-bit',
    sampleRate: '192.0 kHz',
    bitrate: '4,480 kbps',
    hires: true,
    starred: false,
    duration: 212,
    artStyle: 'vinyl-sage',
    colors: ['#2ad18b', '#132620', '#e9efe9'],
    synth: { bpm: 78, root: 62, scale: [0, 2, 3, 7, 9, 12, 14], chords: [[0,3,7,10],[5,8,12,15],[7,10,14,17],[0,3,7,10]], wave: 'sine', feel: 'jazz' },
    waveform: generateWaveform(3.8),
  },
  {
    id: 'trk-6',
    albumId: 'alb-pocket-hifi',
    trackNo: 3,
    title: '杉とクロームテープ',
    artist: '佐藤ミナ・トリオ',
    album: '鴨川カセット紀行',
    year: 2025,
    addedAt: '2026-10-12',
    format: 'FLAC 24-bit',
    sampleRate: '192.0 kHz',
    bitrate: '4,530 kbps',
    hires: true,
    starred: false,
    duration: 198,
    artStyle: 'vinyl-sage',
    colors: ['#2ad18b', '#132620', '#e9efe9'],
    synth: { bpm: 90, root: 58, scale: [0, 2, 4, 7, 9, 11, 14], chords: [[0,4,7,11],[7,11,14,17],[9,12,16,19],[5,9,12,16]], wave: 'triangle', feel: 'jazz' },
    waveform: generateWaveform(4.2),
  },

  /* ---- ALBUM 3: Integer Scale (Vector Telemetry, 2026) ---- */
  {
    id: 'trk-7',
    albumId: 'alb-integer-scale',
    trackNo: 1,
    title: 'Phosphor Grid 1024',
    artist: 'Vector Telemetry',
    album: 'Integer Scale',
    year: 2026,
    addedAt: '2026-11-05',
    format: 'WAV 24-bit',
    sampleRate: '96.0 kHz',
    bitrate: '4,608 kbps',
    hires: true,
    starred: false,
    duration: 198,
    artStyle: 'vfd-grid',
    colors: ['#38f2c6', '#0a1821', '#ff3b5c'],
    synth: { bpm: 114, root: 52, scale: [0, 2, 3, 7, 10, 12, 14], chords: [[0,3,7,10],[8,12,15,19],[5,8,12,15],[7,10,14,17]], wave: 'sawtooth', feel: 'arp' },
    waveform: generateWaveform(4.9),
    lyrics: [
      { t: 0, text: '[SYNTH ARPEGGIO INITIALIZING — 4× INTEGER SCALE]' },
      { t: 10, text: 'Cyan phosphor burning bright against obsidian glass' },
      { t: 22, text: 'Frequency towers dancing clean at sixty frames' },
      { t: 38, text: '[SIGNAL PATH CLEAR — BIT-PERFECT OUTPUT]' },
    ],
  },
  {
    id: 'trk-8',
    albumId: 'alb-integer-scale',
    trackNo: 2,
    title: 'Sub-Zero Reference',
    artist: 'Vector Telemetry',
    album: 'Integer Scale',
    year: 2026,
    addedAt: '2026-11-05',
    format: 'WAV 24-bit',
    sampleRate: '96.0 kHz',
    bitrate: '4,608 kbps',
    hires: true,
    starred: true,
    duration: 248,
    artStyle: 'vfd-grid',
    colors: ['#38f2c6', '#0a1821', '#ff3b5c'],
    synth: { bpm: 108, root: 48, scale: [0, 3, 5, 7, 10, 12, 15], chords: [[0,3,7,10],[0,3,7,10],[5,8,12,15],[3,7,10,14]], wave: 'sine', feel: 'dub' },
    waveform: generateWaveform(5.7),
    lyrics: [
      { t: 0, text: 'Sub-bass horizon pulsing through 3.5mm reference drivers' },
      { t: 16, text: 'Direct path from lossless storage straight to ear' },
    ],
  },
  {
    id: 'trk-9',
    albumId: 'alb-integer-scale',
    trackNo: 3,
    title: 'VFD Horizon',
    artist: 'Vector Telemetry',
    album: 'Integer Scale',
    year: 2026,
    addedAt: '2026-11-05',
    format: 'WAV 24-bit',
    sampleRate: '96.0 kHz',
    bitrate: '4,608 kbps',
    hires: true,
    starred: false,
    duration: 186,
    artStyle: 'vfd-grid',
    colors: ['#38f2c6', '#0a1821', '#ff3b5c'],
    synth: { bpm: 118, root: 55, scale: [0, 2, 3, 7, 10, 12, 14], chords: [[0,3,7,10],[5,8,12,15],[7,10,14,17],[3,7,10,14]], wave: 'sawtooth', feel: 'arp' },
    waveform: generateWaveform(6.3),
    lyrics: [
      { t: 0, text: 'Vacuum fluorescent dusk across Tokyo bay' },
    ],
  },

  /* ---- ALBUM 4: 深夜蛍光管セッション (高橋コウジ, 2024) ---- */
  {
    id: 'trk-10',
    albumId: 'alb-objects-utility',
    trackNo: 1,
    title: '真夜中の真空管',
    artist: '高橋コウジ',
    album: '深夜蛍光管セッション',
    year: 2024,
    addedAt: '2025-08-19',
    format: 'ALAC 16-bit',
    sampleRate: '44.1 kHz',
    bitrate: '982 kbps',
    hires: false,
    starred: false,
    duration: 205,
    artStyle: 'braun-swiss',
    colors: ['#e6481e', '#e8e2d5', '#1b1d22'],
    synth: { bpm: 76, root: 62, scale: [0, 2, 4, 7, 9, 12, 14], chords: [[0,4,7,12],[7,11,14,19],[9,12,16,21],[5,9,12,17]], wave: 'triangle', feel: 'minimal' },
    waveform: generateWaveform(7.1),
  },
  {
    id: 'trk-11',
    albumId: 'alb-objects-utility',
    trackNo: 2,
    title: '新幹線ガラスの雨',
    artist: '高橋コウジ',
    album: '深夜蛍光管セッション',
    year: 2024,
    addedAt: '2025-08-19',
    format: 'ALAC 16-bit',
    sampleRate: '44.1 kHz',
    bitrate: '940 kbps',
    hires: false,
    starred: false,
    duration: 184,
    artStyle: 'braun-swiss',
    colors: ['#e6481e', '#e8e2d5', '#1b1d22'],
    synth: { bpm: 88, root: 55, scale: [0, 2, 5, 7, 9, 12, 14], chords: [[0,5,7,12],[9,12,14,19],[5,7,9,14],[7,12,14,19]], wave: 'sine', feel: 'lofi' },
    waveform: generateWaveform(8.2),
  },
];

/* ---- Deterministic 100 / 2,000-Album Catalog (50% Japanese / 50% English) ---- */
function generateLargeLibraryTracks(targetAlbums = 2000) {
  const prefixes = [
    'Amber', 'Boreal', 'Cedar', 'Drift', 'Echo', 'Fjord', 'Glass', 'Harbor',
    'Indigo', 'Juniper', 'Koto', 'Lunar', 'Monolith', 'Nocturne', 'Opal', 'Prism',
    'Quartz', 'Radian', 'Solstice', 'Tactile', 'Umbra', 'Velvet', 'Wavelength', 'Xenon', 'Yoru', 'Zenith',
  ];
  const nouns = [
    'Sessions', 'Archive', 'Tape', 'Studies', 'Nocturnes', 'Horizons', 'Fidelity',
    'Echoes', 'Sketches', 'Fragments', 'Suites', 'Field Notes', 'Cycles', 'Etudes',
  ];
  const jpPrefixes = [
    '深夜', '琥珀', '雨音', '月光', '東京湾', '硝子', '蛍光管', '海岸線',
    '終電', '星屑', '白昼夢', '秋風', '渋谷', '薄明', '霧雨', '青白い',
    '静寂', '残響', '黄昏', '流星', '真夜中', '銀色', '夜行', '水平線',
  ];
  const jpNouns = [
    'セッション', 'アーカイブ', '夜想曲', 'カセット', 'スケッチ', '録音集',
    '断片', '組曲', '記憶', '風景', '放送', '旅路', '紀行', '小品集',
  ];
  const enArtists = [
    'Bram Vermeer', 'Clara Lindqvist', 'Elena Rostova', 'Felix Brandt',
    'Ivan Petrov', 'Kōji Takahashi', 'Lukas Lindner', 'Nadia Okafor',
    'Otto Klein', 'Pia Sorensen', 'Vector Telemetry', 'Zara Sterling',
  ];
  const jpArtists = [
    '中村アオイ', '森ダイキ', '小林ハナ', '佐藤ジュンイチ', '佐藤ミナ・トリオ',
    '渡辺ユキ', '藤本ソラ', '高橋コウジ', '高橋レン', '田辺ゴロウ', '山田タロウ', '佐々木レイ',
  ];
  const styles = ['bauhaus-amber', 'vinyl-sage', 'vfd-grid', 'braun-swiss'];
  const palettes = [
    ['#ff7a1a', '#1b2234', '#f2e8d5'],
    ['#2ad18b', '#132620', '#e9efe9'],
    ['#38f2c6', '#0a1821', '#ff3b5c'],
    ['#e6481e', '#e8e2d5', '#1b1d22'],
    ['#638dff', '#121526', '#f2efe9'],
    ['#f59e0b', '#261822', '#ffd6a5'],
  ];

  const out = DEMO_TRACKS.map((t) => ({ ...t }));
  const baseDate = new Date('2026-11-20T00:00:00Z').getTime();
  const extraAlbums = Math.max(0, targetAlbums - 4);
  const sharedWf = generateWaveform(3.5);

  for (let i = 0; i < extraAlbums; i++) {
    const isJp = i % 2 === 1;
    const halfIdx = Math.floor(i / 2);
    let albTitle;
    let artist;
    let pIdx;
    let nIdx;
    if (isJp) {
      pIdx = halfIdx % jpPrefixes.length;
      nIdx = Math.floor(halfIdx / jpPrefixes.length) % jpNouns.length;
      const vol = Math.floor(halfIdx / (jpPrefixes.length * jpNouns.length)) + 1;
      albTitle = `${jpPrefixes[pIdx]}の${jpNouns[nIdx]}${vol > 1 ? ` 第${vol}集` : ''}`;
      artist = jpArtists[(halfIdx * 5 + pIdx) % jpArtists.length];
    } else {
      pIdx = halfIdx % prefixes.length;
      nIdx = Math.floor(halfIdx / prefixes.length) % nouns.length;
      const vol = Math.floor(halfIdx / (prefixes.length * nouns.length)) + 1;
      albTitle = `${prefixes[pIdx]} ${nouns[nIdx]}${vol > 1 ? ` Vol. ${vol}` : ''}`;
      artist = enArtists[(halfIdx * 5 + pIdx) % enArtists.length];
    }

    const year = 1978 + ((i * 13) % 49); // 1978..2026
    const addedMs = baseDate - (i + 1) * 86400000 * 0.85;
    const addedDate = new Date(addedMs).toISOString().slice(0, 10);
    const albId = `alb-sim-${i + 1}`;
    const style = styles[i % styles.length];
    const colors = palettes[i % palettes.length];
    const fmt = i % 3 === 0 ? 'FLAC 24-bit' : i % 3 === 1 ? 'ALAC 16-bit' : 'WAV 24-bit';
    const rate = i % 3 === 0 ? '96.0 kHz' : i % 3 === 1 ? '44.1 kHz' : '48.0 kHz';

    // Default 100-album catalog:
    // - Half (~50%) have >10 tracks (11..16 tracks)
    // - One-third (~33%) have multiple discs (2 or 3 discs, with per-disc track numbering)
    const isRichAlbum = targetAlbums <= 120 || i < 96;
    const hasManyTracks = isRichAlbum && (Math.floor(i / 2) % 2 === 0);
    const hasMultiDisc = isRichAlbum && (i % 3 === 0);
    const discCount = hasMultiDisc ? (i % 6 === 0 ? 3 : 2) : 1;
    const totalTracks = !isRichAlbum
      ? 1
      : hasManyTracks
        ? (11 + ((i * 3) % 6)) // 11..16 tracks
        : hasMultiDisc
          ? (discCount === 3 ? 9 : 8) // multi-disc album with 3-4 tracks per disc
          : (4 + (i % 5)); // 4..8 tracks

    const basePerDisc = Math.floor(totalTracks / discCount);
    const extraTracks = totalTracks % discCount;

    let globalIdx = 0;
    for (let d = 1; d <= discCount; d++) {
      const countThisDisc = basePerDisc + (d <= extraTracks ? 1 : 0);
      for (let t = 0; t < countThisDisc; t++) {
        let subTitle;
        if (isJp) {
          const jpP = jpPrefixes[(pIdx + globalIdx * 3 + d * 2) % jpPrefixes.length];
          const jpN = jpNouns[(nIdx + globalIdx * 2 + d) % jpNouns.length];
          subTitle = `${jpP}${jpN}`;
        } else {
          subTitle = totalTracks === 1
            ? `${prefixes[(pIdx + 5) % prefixes.length]} ${nouns[(nIdx + 3) % nouns.length]}`
            : `${prefixes[(pIdx + globalIdx * 3 + d * 2) % prefixes.length]} ${nouns[(nIdx + globalIdx * 2 + d) % nouns.length]}`;
        }
        out.push({
          id: `trk-sim-${i + 1}-d${d}-t${t + 1}`,
          albumId: albId,
          discNo: d,
          trackNo: t + 1,
          title: subTitle,
          artist,
          album: albTitle,
          year,
          addedAt: addedDate,
          format: fmt,
          sampleRate: rate,
          bitrate: '2,410 kbps',
          hires: fmt !== 'ALAC 16-bit',
          starred: (i === 2 && globalIdx === 0) || (i === 6 && globalIdx === 1),
          duration: 158 + (((i + 1) * 29 + globalIdx * 37) % 145),
          artStyle: style,
          colors,
          synth: DEMO_TRACKS[(i + globalIdx) % 4].synth,
          waveform: sharedWf,
        });
        globalIdx++;
      }
    }
  }
  return out;
}

const INITIAL_PLAYLISTS = [
  { id: 'pl-desk', name: '深夜デスク録音', trackIds: ['trk-1', 'trk-4', 'trk-10'] },
  { id: 'pl-commute', name: 'Commute Hi-Res', trackIds: ['trk-7', 'trk-8', 'trk-2', 'trk-5'] },
];

function midiToFreq(midi) {
  return 440 * Math.pow(2, (midi - 69) / 12);
}

class DeckAudioEngine {
  constructor() {
    this.ctx = null;
    this.isPlaying = false;
    this.currentTime = 0;
    this.duration = 216;
    this.track = DEMO_TRACKS[0];
    this.volume = 0.76;

    this.audioEl = null;
    this.mediaSource = null;
    this.synthTimer = null;
    this.synthStep = 0;
    this.lastFrameTs = 0;

    this.masterGain = null;
    this.analyserMain = null;
    this.analyserL = null;
    this.analyserR = null;
    this.synthBus = null;

    this.fftData = new Uint8Array(512);
    this.waveData = new Uint8Array(512);
    this.leftBuf = new Float32Array(256);
    this.rightBuf = new Float32Array(256);
  }

  ensureContext() {
    if (this.ctx) {
      if (this.ctx.state === 'suspended') this.ctx.resume();
      return;
    }

    const AudioCtx = window.AudioContext || window.webkitAudioContext;
    this.ctx = new AudioCtx();

    this.synthBus = this.ctx.createGain();
    this.synthBus.gain.value = 0.9;

    // Pre-fader unity-gain source bus so VU & Spectrum always meter the audio file itself (independent of volume/mute)
    this.sourceBus = this.ctx.createGain();
    this.sourceBus.gain.value = 1.0;

    this.masterGain = this.ctx.createGain();
    this.masterGain.gain.value = this.volume;

    this.analyserMain = this.ctx.createAnalyser();
    this.analyserMain.fftSize = 1024;
    this.analyserMain.smoothingTimeConstant = 0.8;

    const splitter = this.ctx.createChannelSplitter(2);
    this.analyserL = this.ctx.createAnalyser();
    this.analyserR = this.ctx.createAnalyser();
    this.analyserL.fftSize = 256;
    this.analyserR.fftSize = 256;

    this.synthBus.connect(this.sourceBus);
    this.sourceBus.connect(this.analyserMain);
    this.sourceBus.connect(splitter);
    splitter.connect(this.analyserL, 0);
    splitter.connect(this.analyserR, 1);
    this.sourceBus.connect(this.masterGain);
    this.masterGain.connect(this.ctx.destination);

    this.audioEl = new Audio();
    this.audioEl.crossOrigin = 'anonymous';
    this.mediaSource = this.ctx.createMediaElementSource(this.audioEl);
    this.mediaSource.connect(this.sourceBus);

    this.fftData = new Uint8Array(this.analyserMain.frequencyBinCount);
    this.waveData = new Uint8Array(this.analyserMain.frequencyBinCount);
  }

  loadTrack(track, autoPlay = false) {
    this.stopSynth();
    if (this.audioEl) {
      this.audioEl.pause();
      this.audioEl.currentTime = 0;
    }
    this.track = track;
    this.duration = track.duration || 180;
    this.currentTime = 0;
    this.synthStep = 0;

    if (track.localUrl) {
      this.ensureContext();
      this.audioEl.src = track.localUrl;
      this.audioEl.load();
    }

    if (autoPlay) this.play();
    else this.isPlaying = false;
  }

  play() {
    this.ensureContext();
    this.isPlaying = true;
    this.lastFrameTs = performance.now();
    if (this.track.localUrl) this.audioEl.play().catch(() => {});
    else this.startSynth();
  }

  pause() {
    this.isPlaying = false;
    this.stopSynth();
    if (this.audioEl && this.track.localUrl) this.audioEl.pause();
  }

  togglePlay() {
    if (this.isPlaying) this.pause();
    else this.play();
    return this.isPlaying;
  }

  seek(seconds) {
    const clamped = Math.max(0, Math.min(this.duration, seconds));
    this.currentTime = clamped;
    if (this.audioEl && this.track.localUrl) this.audioEl.currentTime = clamped;
  }

  setVolume(norm) {
    this.volume = Math.max(0, Math.min(1, norm));
    if (this.masterGain && this.ctx) {
      this.masterGain.gain.setTargetAtTime(this.volume, this.ctx.currentTime, 0.02);
    }
  }

  tickTime(onTrackEnd) {
    if (!this.isPlaying) return this.currentTime;
    if (this.track.localUrl && this.audioEl) {
      this.currentTime = this.audioEl.currentTime || 0;
      if (this.audioEl.duration && !Number.isNaN(this.audioEl.duration)) {
        this.duration = this.audioEl.duration;
      }
      if (this.audioEl.ended) onTrackEnd();
    } else {
      const now = performance.now();
      const dt = (now - this.lastFrameTs) / 1000;
      this.lastFrameTs = now;
      this.currentTime += Math.min(dt, 0.2);
      if (this.currentTime >= this.duration) {
        this.currentTime = 0;
        onTrackEnd();
      }
    }
    return this.currentTime;
  }

  startSynth() {
    this.stopSynth();
    const spec = this.track.synth || DEMO_TRACKS[0].synth;
    const stepMs = (60 / spec.bpm / 2) * 1000;

    const triggerStep = () => {
      if (!this.isPlaying || !this.ctx) return;
      const now = this.ctx.currentTime;
      const s = this.synthStep++;
      const chordIdx = Math.floor(s / 8) % spec.chords.length;
      const chord = spec.chords[chordIdx];

      if (s % 8 === 0) {
        chord.forEach((semi, idx) => {
          const osc = this.ctx.createOscillator();
          const gain = this.ctx.createGain();
          const panner = this.ctx.createStereoPanner();
          const lp = this.ctx.createBiquadFilter();

          osc.type = idx === 0 ? 'sine' : spec.wave;
          osc.frequency.setValueAtTime(midiToFreq(spec.root + semi), now);
          osc.detune.setValueAtTime((idx - 1.5) * 4.5, now);

          lp.type = 'lowpass';
          lp.frequency.setValueAtTime(spec.feel === 'arp' ? 2200 : 1250, now);
          lp.frequency.exponentialRampToValueAtTime(620, now + stepMs * 0.0075);
          panner.pan.setValueAtTime((idx % 2 === 0 ? -1 : 1) * 0.4, now);

          const amp = spec.feel === 'arp' ? 0.055 : 0.075;
          gain.gain.setValueAtTime(0.0001, now);
          gain.gain.linearRampToValueAtTime(amp, now + 0.045);
          gain.gain.exponentialRampToValueAtTime(0.0008, now + (stepMs / 1000) * 7.8);

          osc.connect(lp);
          lp.connect(gain);
          gain.connect(panner);
          panner.connect(this.synthBus);
          osc.start(now);
          osc.stop(now + (stepMs / 1000) * 8);
        });
      }

      if (s % 4 === 0 || (spec.feel === 'dub' && s % 8 === 6)) {
        const bass = this.ctx.createOscillator();
        const bg = this.ctx.createGain();
        bass.type = 'sine';
        bass.frequency.setValueAtTime(midiToFreq(spec.root + chord[0] - 12), now);
        bg.gain.setValueAtTime(0.001, now);
        bg.gain.linearRampToValueAtTime(0.22, now + 0.02);
        bg.gain.exponentialRampToValueAtTime(0.001, now + 0.42);
        bass.connect(bg);
        bg.connect(this.synthBus);
        bass.start(now);
        bass.stop(now + 0.46);
      }

      const playPluck = spec.feel === 'arp' ? true : (s % 2 === 1 && Math.sin(s * 1.7) > -0.25);
      if (playPluck) {
        const noteIdx = (s * 3 + chordIdx) % spec.scale.length;
        const osc = this.ctx.createOscillator();
        const g = this.ctx.createGain();
        const p = this.ctx.createStereoPanner();
        osc.type = spec.feel === 'jazz' ? 'sine' : 'triangle';
        osc.frequency.setValueAtTime(midiToFreq(spec.root + 12 + spec.scale[noteIdx]), now);
        p.pan.setValueAtTime(Math.sin(s * 0.9) * 0.45, now);
        g.gain.setValueAtTime(0.0001, now);
        g.gain.linearRampToValueAtTime(0.08, now + 0.012);
        g.gain.exponentialRampToValueAtTime(0.0008, now + 0.34);
        osc.connect(g);
        g.connect(p);
        p.connect(this.synthBus);
        osc.start(now);
        osc.stop(now + 0.38);
      }

      if (spec.feel !== 'minimal') {
        if (s % 4 === 0) {
          const kick = this.ctx.createOscillator();
          const kg = this.ctx.createGain();
          kick.type = 'sine';
          kick.frequency.setValueAtTime(125, now);
          kick.frequency.exponentialRampToValueAtTime(42, now + 0.11);
          kg.gain.setValueAtTime(0.28, now);
          kg.gain.exponentialRampToValueAtTime(0.001, now + 0.16);
          kick.connect(kg);
          kg.connect(this.synthBus);
          kick.start(now);
          kick.stop(now + 0.18);
        }
        if (s % 2 === 1) {
          const bufSize = Math.floor(this.ctx.sampleRate * 0.035);
          const buf = this.ctx.createBuffer(1, bufSize, this.ctx.sampleRate);
          const data = buf.getChannelData(0);
          for (let i = 0; i < bufSize; i++) data[i] = (Math.random() * 2 - 1) * (1 - i / bufSize);
          const noise = this.ctx.createBufferSource();
          noise.buffer = buf;
          const hp = this.ctx.createBiquadFilter();
          hp.type = 'highpass';
          hp.frequency.value = 6500;
          const ng = this.ctx.createGain();
          ng.gain.setValueAtTime(s % 4 === 2 ? 0.065 : 0.032, now);
          ng.gain.exponentialRampToValueAtTime(0.001, now + 0.035);
          noise.connect(hp);
          hp.connect(ng);
          ng.connect(this.synthBus);
          noise.start(now);
        }
      }
    };

    triggerStep();
    this.synthTimer = setInterval(triggerStep, stepMs);
  }

  stopSynth() {
    if (this.synthTimer) {
      clearInterval(this.synthTimer);
      this.synthTimer = null;
    }
  }

  getStereoLevels() {
    if (!this.analyserL || !this.isPlaying) return { left: 0, right: 0 };
    this.analyserL.getFloatTimeDomainData(this.leftBuf);
    this.analyserR.getFloatTimeDomainData(this.rightBuf);

    let sumL = 0;
    let sumR = 0;
    for (let i = 0; i < this.leftBuf.length; i++) {
      sumL += this.leftBuf[i] * this.leftBuf[i];
      sumR += this.rightBuf[i] * this.rightBuf[i];
    }
    const rmsL = Math.sqrt(sumL / this.leftBuf.length);
    const rmsR = Math.sqrt(sumR / this.rightBuf.length);
    const dbL = rmsL > 0.0001 ? Math.max(-60, 20 * Math.log10(rmsL * 2.6)) : -60;
    const dbR = rmsR > 0.0001 ? Math.max(-60, 20 * Math.log10(rmsR * 2.6)) : -60;

    return {
      left: Math.min(1, Math.max(0, (dbL + 48) / 48)),
      right: Math.min(1, Math.max(0, (dbR + 48) / 48)),
    };
  }

  getSpectrumAndWave() {
    if (!this.analyserMain || !this.isPlaying) {
      this.fftData.fill(0);
      this.waveData.fill(128);
      return { fft: this.fftData, wave: this.waveData };
    }
    this.analyserMain.getByteFrequencyData(this.fftData);
    this.analyserMain.getByteTimeDomainData(this.waveData);
    return { fft: this.fftData, wave: this.waveData };
  }

  /* ---- Nintendo-Style Tactile UI Sound FX Synthesizer (DSi / 3DS / Switch feel) ---- */
  playSfx(type = 'tick') {
    this.ensureContext();
    if (!this.ctx) return;
    const ctx = this.ctx;
    const now = ctx.currentTime;

    const tone = (freqStart, freqEnd, dur, vol, wave = 'sine', delay = 0) => {
      const t0 = now + delay;
      const osc = ctx.createOscillator();
      const gain = ctx.createGain();
      osc.type = wave;
      osc.frequency.setValueAtTime(freqStart, t0);
      if (freqEnd !== freqStart) {
        osc.frequency.exponentialRampToValueAtTime(Math.max(40, freqEnd), t0 + dur);
      }
      gain.gain.setValueAtTime(0.0001, t0);
      gain.gain.linearRampToValueAtTime(vol, t0 + 0.006);
      gain.gain.exponentialRampToValueAtTime(0.0001, t0 + dur);
      osc.connect(gain);
      gain.connect(ctx.destination);
      osc.start(t0);
      osc.stop(t0 + dur + 0.01);
    };

    if (type === 'press') {
      // Crisp mechanical dome switch press-down ("tock")
      tone(520, 310, 0.024, 0.075, 'triangle', 0);
      tone(1240, 880, 0.012, 0.032, 'sine', 0);
    } else if (type === 'release') {
      // Lighter spring-back release ("tick-up")
      tone(760, 1080, 0.022, 0.055, 'triangle', 0);
      tone(1680, 2100, 0.012, 0.025, 'sine', 0.004);
    } else if (type === 'tick') {
      // Soft glassy wooden pop (D-Pad / tap)
      tone(920, 1280, 0.028, 0.08, 'triangle', 0);
    } else if (type === 'deck:down') {
      tone(420, 260, 0.022, 0.11, 'triangle', 0);
    } else if (type === 'deck:up') {
      tone(1560, 1840, 0.026, 0.065, 'sine', 0);
    } else if (type === 'deck') {
      // Crisp Switch Joy-Con click snap (L1 / R1 / Deck switch)
      tone(420, 260, 0.022, 0.11, 'triangle', 0);
      tone(1560, 1840, 0.026, 0.06, 'sine', 0.008);
    } else if (type === 'confirm:down') {
      tone(1046.5, 1046.5, 0.065, 0.09, 'sine', 0);
    } else if (type === 'confirm:up') {
      tone(1568.0, 1568.0, 0.095, 0.085, 'triangle', 0);
    } else if (type === 'confirm') {
      // Classic Nintendo warm two-note chime (C6 -> G6)
      tone(1046.5, 1046.5, 0.065, 0.09, 'sine', 0);
      tone(1568.0, 1568.0, 0.095, 0.085, 'triangle', 0.045);
    } else if (type === 'back:down') {
      tone(784.0, 740.0, 0.045, 0.08, 'triangle', 0);
    } else if (type === 'back:up') {
      tone(587.3, 520.0, 0.065, 0.07, 'sine', 0);
    } else if (type === 'back') {
      // Gentle downward wooden pluck (G5 -> D5)
      tone(784.0, 740.0, 0.045, 0.08, 'triangle', 0);
      tone(587.3, 520.0, 0.065, 0.07, 'sine', 0.036);
    } else if (type === 'play:down') {
      tone(784.0, 784.0, 0.055, 0.075, 'sine', 0);
    } else if (type === 'play:up') {
      tone(1174.7, 1174.7, 0.085, 0.08, 'triangle', 0);
    } else if (type === 'play') {
      // Bright rising 2-tone fifth (G5 -> D6, mirror of Pause)
      tone(784.0, 784.0, 0.055, 0.075, 'sine', 0);
      tone(1174.7, 1174.7, 0.085, 0.08, 'triangle', 0.048);
    } else if (type === 'pause:down') {
      tone(1174.7, 1140.0, 0.055, 0.07, 'sine', 0);
    } else if (type === 'pause:up') {
      tone(784.0, 760.0, 0.08, 0.07, 'triangle', 0);
    } else if (type === 'pause') {
      // Soft damped descending pair (D6 -> G5)
      tone(1174.7, 1140.0, 0.055, 0.07, 'sine', 0);
      tone(784.0, 760.0, 0.08, 0.07, 'triangle', 0.048);
    } else if (type === 'star') {
      // Sparkly Nintendo star collect chime (C6 -> E6 -> G6 -> C7)
      tone(1046.5, 1046.5, 0.055, 0.08, 'sine', 0);
      tone(1318.5, 1318.5, 0.055, 0.08, 'sine', 0.038);
      tone(1568.0, 1568.0, 0.06, 0.08, 'sine', 0.076);
      tone(2093.0, 2093.0, 0.13, 0.085, 'triangle', 0.114);
    } else if (type === 'unstar') {
      tone(659.3, 523.3, 0.065, 0.07, 'triangle', 0);
    } else if (type === 'stage:down') {
      tone(1174.7, 1568.0, 0.065, 0.075, 'sine', 0);
    } else if (type === 'stage:up') {
      tone(1760.0, 1760.0, 0.075, 0.06, 'triangle', 0);
    } else if (type === 'stage') {
      // Crisp glass shimmer for Top Stage switch
      tone(1174.7, 1760.0, 0.075, 0.075, 'sine', 0);
      tone(1760.0, 1760.0, 0.07, 0.05, 'triangle', 0.04);
    } else if (type === 'add:down') {
      tone(1396.9, 1396.9, 0.05, 0.08, 'sine', 0);
    } else if (type === 'add:up') {
      tone(1760.0, 1760.0, 0.08, 0.08, 'sine', 0);
    } else if (type === 'add') {
      // Cheerful bubble confirm (F6 -> A6)
      tone(1396.9, 1396.9, 0.05, 0.08, 'sine', 0);
      tone(1760.0, 1760.0, 0.08, 0.08, 'sine', 0.04);
    } else if (type === 'dim') {
      // Subtle soft power-dim tone
      tone(660, 440, 0.08, 0.07, 'sine', 0);
    } else if (type === 'bump') {
      // Soft Nintendo DS boundary thud when hitting L1/R1 edge
      tone(210, 140, 0.055, 0.09, 'triangle', 0);
    }
  }
}

window.DECK_AUDIO = {
  DEMO_TRACKS,
  INITIAL_PLAYLISTS,
  generateWaveform,
  generateLargeLibraryTracks,
  DeckAudioEngine,
};
