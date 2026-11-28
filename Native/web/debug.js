const byId = (id) => document.getElementById(id);
const format = new Intl.NumberFormat(undefined, { maximumFractionDigits: 2 });
const priorities = ["", "TRACE", "VERBOSE", "DEBUG", "INFO", "WARN", "ERROR", "CRITICAL"];
const powerNames = {
  onBattery: "On battery", noBattery: "No battery", charging: "Charging", charged: "Charged",
  error: "Error", unknown: "Unknown"
};

function fields(id, values, live) {
  const list = byId(id);
  values.forEach(([label, value], index) => {
    let detail = list.children[index * 2 + 1];
    if (!detail) {
      const key = document.createElement("dt");
      key.className = "text-body-secondary fw-normal";
      key.textContent = key.title = label;
      detail = document.createElement("dd");
      list.append(key, detail);
    }
    const text = live ? String(value ?? "Unavailable") : "—";
    if (detail.textContent !== text) detail.textContent = detail.title = text;
  });
}

function withUnit(value, unit) {
  return value == null ? null : `${format.format(value)} ${unit}`;
}

function show(report) {
  const { app = {}, system = {}, audio = {}, video = {}, input = {}, library = {}, logs = [] } = report ?? {};
  if (report) {
    const status = `Live · ${app.mode} · ${app.version}`;
    const heading = byId("debug-status");
    if (heading.textContent !== status) heading.textContent = heading.title = status;
  }
  fields("system-fields", [
    ["Device", system.model || system.machine],
    ["Platform", [system.platform, system.kernel].filter(Boolean).join(" ")],
    ["SDL", app.sdlVersion],
    ["Uptime", withUnit(app.uptimeMs / 1000, "s")],
    ["Frame interval", withUnit(app.frameMs, "ms")],
    ["Logical CPUs", system.logicalCpus],
    ["Load average (1 min)", system.loadAverage1m == null ? null : format.format(system.loadAverage1m)],
    ["System RAM", withUnit(system.ramMiB, "MiB")],
    ["Peak DeckDS memory", system.peakRssBytes ? withUnit(system.peakRssBytes / 1048576, "MiB") : null],
    ["Power", [powerNames[system.powerState], system.batteryPercent == null ? "" : `${system.batteryPercent}%`]
      .filter(Boolean).join(" / ")],
    ["Wi-Fi", system.wifiConnected ? "Connected" : "Disconnected"],
    ["Bluetooth", system.bluetoothConnected ? "Connected" : "Disconnected"],
    ["Lid", system.lidClosed ? "Closed" : "Open"]
  ], report);
  fields("audio-fields", [
    ["Driver", audio.driver || null],
    ["Playback", audio.playing ? "Playing" : "Idle"],
    ["Source", [audio.codec, audio.sourceRate].filter(Boolean).join(" / ") || null],
    ["Decoder rate", withUnit(audio.decoderHz, "Hz")],
    ["Output rate", withUnit(audio.deviceHz, "Hz")],
    ["Output channels", audio.deviceChannels],
    ["Device buffer", withUnit(audio.deviceBufferFrames, "frames")]
  ], report);
  const button = input.lastControllerEvent;
  const displays = video.displays?.map((display) =>
    [display.name, `${display.width} × ${display.height}`, withUnit(display.refreshHz, "Hz")].filter(Boolean).join(" / "));
  const windows = video.windows?.map((window) => `${window.panel}: ${window.renderer} / ${window.width} × ${window.height}`);
  fields("display-fields", [
    ["Video driver", video.driver || null],
    ["Displays", displays?.join(" · ") || "None"],
    ["Windows", windows?.join(" · ") || "None"],
    ["Deck displays", system.displaysOff ? "Off" : "On"],
    ["Touch devices", input.touchDevices],
    ["Controllers", input.gamepads?.join(", ") || "None"],
    ["Last controller event", button ? `${button.button} ${button.pressed ? "pressed" : "released"}` : "None"]
  ], report);
  fields("library-fields", [
    ["Indexed tracks", format.format(library.tracks)],
    ["Up Next", format.format(library.queue)],
    ["Missing files awaiting review", format.format(library.missing)],
    ["Scan", library.scan ? `${format.format(library.scan.checked)} / ${library.scan.total ? format.format(library.scan.total) : "?"}` : "Idle"],
    ["Status", library.status || "—"]
  ], report);
  const logText = logs.map((entry) =>
    `${(entry.timeMs / 1000).toFixed(2)}s  ${priorities[entry.priority] || entry.priority}  ${entry.message}`
  ).join("\n") || "No DeckDS log messages yet.";
  const logPane = byId("logs");
  if (logPane.textContent !== logText) {
    const offset = logPane.scrollTop;
    logPane.textContent = logText;
    logPane.scrollTop = offset;
  }
}

async function poll() {
  try {
    const response = await fetch("/api/debug", { cache: "no-store" });
    if (!response.ok) throw new Error("Debug snapshot unavailable");
    show(await response.json());
  } catch {
    const status = byId("debug-status");
    if (status.textContent !== "Waiting for DeckDS…") status.textContent = status.title = "Waiting for DeckDS…";
  } finally {
    setTimeout(poll, 1000);
  }
}

show();
poll();
