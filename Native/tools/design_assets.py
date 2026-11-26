"""Build native fixtures from the web reference without opening a browser."""
import ast
import json
import re
import subprocess
import sys
from pathlib import Path


def cpp(value):
    return json.dumps(value, ensure_ascii=False)


def js_array(source, name):
    return source.split(f"const {name} = [", 1)[1].split("\n];", 1)[0]


def reference_library(node, path):
    # Execute only at build time; the running SDL app uses C++ data.
    source = """
const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.readFileSync(process.argv[1], 'utf8');
const context = { window: {} };
vm.runInNewContext(source, context, { filename: process.argv[1] });
const engine = context.window.DECK_AUDIO;
const normal = engine.generateLargeLibraryTracks(100);
process.stdout.write(JSON.stringify({
  defaultSize: normal.length,
  tracks: engine.generateLargeLibraryTracks(2000).map(({waveform, ...track}) => track),
}));
"""
    return json.loads(subprocess.check_output([node, "-e", source, str(path)], text=True))


def keyboard_rows(node, path):
    source = r"""
const fs = require('node:fs');
const vm = require('node:vm');
const app = fs.readFileSync(process.argv[1], 'utf8');
const start = app.indexOf('const OSK_ROWS = {');
const end = app.indexOf('function getOskRowKeys(', start);
if (start < 0 || end < 0) throw new Error('Missing OSK_ROWS in the reference');
process.stdout.write(JSON.stringify(vm.runInNewContext(app.slice(start, end) + '\nOSK_ROWS')));
"""
    layouts = json.loads(subprocess.check_output([node, "-e", source, str(path)], text=True))
    rows = []
    for mode, keyboard in layouts.items():
        for row, keys in enumerate(keyboard):
            column = 0
            for key in keys:
                cells = [cpp(mode == "num"), str(row), str(column), str(key["span"]),
                         cpp(key["label"]), cpp(key.get("val", "")), cpp(key.get("act", "")),
                         cpp(key.get("fn", False)), cpp(key.get("done", False))]
                rows.append("{" + ", ".join(cells) + "}")
                column += key["span"]
            if column != 10:
                raise ValueError(f"Unexpected keypad row width in reference {mode}")
    return rows

def track_rows(tracks, source):
    seeds = re.findall(r"waveform: generateWaveform\(([\d.]+)\)", js_array(source, "DEMO_TRACKS"))
    if not tracks or len(seeds) > len(tracks):
        raise ValueError("Malformed reference library")
    rows = []
    for index, track in enumerate(tracks):
        strings = [track[name] for name in
                   ("id", "title", "artist", "album", "format", "sampleRate", "bitrate", "addedAt", "artStyle")]
        numbers = [track.get("discNo", 1), *(track[name] for name in ("trackNo", "year", "duration"))]
        colors = "{" + ", ".join(map(cpp, track["colors"])) + "}"
        seed = seeds[index] if index < len(seeds) else "3.5"
        rows.append("{" + ", ".join([
            *map(cpp, strings), *map(str, numbers), colors, seed + "f", str(track["synth"]["bpm"]),
            str(track["synth"]["root"]), cpp(track["starred"]),
        ]) + "}")
    return rows


def sound_rows(source):
    cues = source.split("playSfx(type = 'tick')", 1)[1]
    matches = list(re.finditer(r"if \(type === '([^']+)'\) \{", cues))
    rows = []
    for index, match in enumerate(matches):
        end = matches[index + 1].start() if index + 1 < len(matches) else len(cues)
        pattern = r"tone\(([\d.]+), ([\d.]+), ([\d.]+), ([\d.]+), '(sine|triangle)', ([\d.]+)\);"
        for start, stop, duration, gain, shape, delay in re.findall(pattern, cues[match.end():end]):
            rows.append("{" + ", ".join([
                cpp(match[1]), start, stop, duration, gain, delay, cpp(shape == "triangle"),
            ]) + "}")
    if not rows:
        raise ValueError("Missing UI sounds in the browser reference")
    return rows


def playlist_rows(source):
    pattern = r"name: ('(?:[^'\\]|\\.)*'), trackIds: (\[[^\]]*\])"
    return ["{" + cpp(ast.literal_eval(name)) + ", {" +
            ", ".join(map(cpp, ast.literal_eval(ids))) + "}}"
            for name, ids in re.findall(pattern, js_array(source, "INITIAL_PLAYLISTS"))]


def svg_icons(html):
    pattern = r'<symbol id="ic-([^"]+)" viewBox="([^"]+)">(.*?)</symbol>'
    return ["{" + cpp(name) + ', R"decksvg(' +
            f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{box}" fill="none">' +
            nodes.replace("currentColor", "#ffffff") + '</svg>)decksvg"}'
            for name, box, nodes in re.findall(pattern, html, re.S)]


def vfd_icons(node, path):
    source = r"""
const fs = require('node:fs');
const vm = require('node:vm');
const code = fs.readFileSync(process.argv[1], 'utf8');
const symbols = [];
const hostSvg = { appendChild(sym) { symbols.push(sym); } };
const context = {
  window: {},
  document: {
    querySelector() { return hostSvg; },
    createElementNS(_ns, tag) {
      return {
        tag,
        attrs: {},
        children: [],
        setAttribute(k, v) { this.attrs[k] = v; },
        appendChild(c) { this.children.push(c); },
      };
    },
  },
};
vm.runInNewContext(code, context, { filename: process.argv[1] });
const out = [];
for (const sym of symbols) {
  const m = /^vfd(\d+)-ic-(.+)$/.exec(sym.attrs.id || '');
  if (!m) continue;
  const size = Number(m[1]);
  const name = m[2];
  const grid = Array.from({ length: size }, () => Array(size).fill('.'));
  const d = sym.children[0]?.attrs?.d || '';
  for (const hit of d.matchAll(/M(\d+) (\d+)h1v1h-1z/g)) {
    const x = Number(hit[1]), y = Number(hit[2]);
    if (x >= 0 && x < size && y >= 0 && y < size) grid[y][x] = '#';
  }
  out.push({ name, size, dots: grid.map(r => r.join('')).join('') });
}
process.stdout.write(JSON.stringify(out));
"""
    icons = json.loads(subprocess.check_output([node, "-e", source, str(path)], text=True))
    if not icons:
        raise ValueError("Missing VFD pixel icons in the browser reference")
    return [f"{{{cpp(icon['name'])}, {icon['size']}, {cpp(icon['dots'])}}}" for icon in icons]


def array(type_name, name, rows):
    storage = "const" if type_name == "PlaylistFixture" else "constexpr"
    return f"inline {storage} {type_name} {name}[] = {{\n    " + ",\n    ".join(rows) + "\n};\n"


def main():
    design, output = map(Path, sys.argv[1:3])
    js = design / "audio-engine.js"
    source = js.read_text(encoding="utf-8")
    library = reference_library(sys.argv[3], js)
    icons = svg_icons((design / "index.html").read_text(encoding="utf-8"))
    for icon in map(Path, sys.argv[4:]):
        svg = icon.read_text(encoding="utf-8").strip()
        icons.append("{" + cpp(icon.stem) + ', R"decksvg(' + svg + ')decksvg"}')
    output.write_text(
        "// Generated DeckDS assets.\n" +
        f"inline constexpr int defaultLibrarySize = {library['defaultSize']};\n" +
        array("Track", "demoTracks", track_rows(library["tracks"], source)) +
        array("SfxAsset", "sfxAssets", sound_rows(source)) +
        array("KeyAsset", "keyAssets", keyboard_rows(sys.argv[3], design / "app.js")) +
        array("PlaylistFixture", "demoPlaylists", playlist_rows(source)) +
        array("IconAsset", "svgAssets", icons) +
        array("PixelAsset", "pixelAssets", vfd_icons(sys.argv[3], design / "vfd-icons.js")),
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
