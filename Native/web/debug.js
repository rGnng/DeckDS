const byId = (id) => document.getElementById(id);
const format = new Intl.NumberFormat(undefined, { maximumFractionDigits: 2 });
const priorities = ["", "TRACE", "VERBOSE", "DEBUG", "INFO", "WARN", "ERROR", "CRITICAL"];
const powerNames = {
  onBattery: "On battery", noBattery: "No battery", charging: "Charging", charged: "Charged",
  error: "Error", unknown: "Unknown"
};

function fields(id, values) {
  const rows = [];
  for (const [label, value] of values) {
    const key = document.createElement("dt");
    const detail = document.createElement("dd");
    key.className = "text-body-secondary fw-normal";
    key.textContent = label;
    detail.textContent = value ?? "Unavailable";
    rows.push(key, detail);
  }
  byId(id).replaceChildren(...rows);
}

function withUnit(value, unit) {
  return value == null ? null : `${format.format(value)} ${unit}`;
}

function show(report) {
  const { app, system, audio, video, input, library, logs } = report;
  byId("debug-status").textContent = `Live / ${app.mode} / DeckDS ${app.version}`;
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
  ]);
  fields("audio-fields", [
    ["Driver", audio.driver || null],
    ["Playback", audio.playing ? "Playing" : "Idle"],
    ["Source", [audio.codec, audio.sourceRate].filter(Boolean).join(" / ") || null],
    ["Decoder rate", withUnit(audio.decoderHz, "Hz")],
    ["Output rate", withUnit(audio.deviceHz, "Hz")],
    ["Output channels", audio.deviceChannels],
    ["Device buffer", withUnit(audio.deviceBufferFrames, "frames")]
  ]);
  const button = input.lastControllerEvent;
  fields("display-fields", [
    ["Video driver", video.driver || null],
    ...video.displays.map((display, index) => [
      `Display ${index + 1}`,
      [display.name, `${display.width} × ${display.height}`, withUnit(display.refreshHz, "Hz")].filter(Boolean).join(" / ")
    ]),
    ...video.windows.map((window) => [
      `Window (${window.panel})`, `${window.renderer} / ${window.width} × ${window.height}`
    ]),
    ["Deck displays", system.displaysOff ? "Off" : "On"],
    ["Touch devices", input.touchDevices],
    ["Controllers", input.gamepads.join(", ") || "None"],
    ["Last controller event", button ? `${button.button} ${button.pressed ? "pressed" : "released"}` : "None"]
  ]);
  fields("library-fields", [
    ["Indexed tracks", format.format(library.tracks)],
    ["Up Next", format.format(library.queue)],
    ["Missing files awaiting review", format.format(library.missing)],
    ["Scan", library.scan ? `${format.format(library.scan.checked)} / ${library.scan.total ? format.format(library.scan.total) : "?"}` : "Idle"],
    ...(library.status ? [["Status", library.status]] : [])
  ]);
  const logText = logs.map((entry) =>
    `${(entry.timeMs / 1000).toFixed(2)}s  ${priorities[entry.priority] || entry.priority}  ${entry.message}`
  ).join("\n") || "No DeckDS log messages yet.";
  const logPane = byId("logs");
  if (logPane.textContent !== logText)
    logPane.textContent = logText;
}

async function poll() {
  try {
    const response = await fetch("/api/debug", { cache: "no-store" });
    if (!response.ok) throw new Error("Debug snapshot unavailable");
    show(await response.json());
  } catch {
    byId("debug-status").textContent = "Waiting for DeckDS…";
  } finally {
    setTimeout(poll, 1000);
  }
}

poll();
