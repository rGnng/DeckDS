const byId = (id) => document.getElementById(id);
const connection = byId("connection");
const scanButtons = [byId("quick"), byId("full")];
const reviewButtons = [byId("keep"), byId("remove")];
const settings = [...document.querySelectorAll("select[data-setting]")];
const previous = byId("previous");
const play = byId("playback-toggle");
const next = byId("next");
const seek = byId("seek");
const volume = byId("volume");
const mute = byId("mute");
const otaEdit = byId("ota-edit");
const accent = byId("accent");
const accessDialog = byId("web-password-dialog");
const accessInput = byId("web-password-input");
const accessEdit = byId("web-password-edit");
const accessSave = byId("web-password-save");
const accessCancel = byId("web-password-cancel");
const accessError = byId("web-password-error");
const wifiDialog = byId("wifi-dialog");
const wifiBrowser = byId("wifi-browser");
const wifiForm = byId("wifi-form");
const wifiClose = byId("wifi-close");
const wifiPassword = byId("wifi-password");
const wifiCancel = byId("wifi-cancel");
const wifiJoin = byId("wifi-join");
const wifiJoinError = byId("wifi-join-error");
const wifiManual = byId("wifi-manual");
const wifiSsid = byId("wifi-ssid");
const wifiName = byId("wifi-name");
const wifiStrength = byId("wifi-strength");
const wifiIcons = [...byId("wifi-link").querySelectorAll("svg")];
const signIn = byId("sign-in");
const bluetoothDialog = byId("bluetooth-dialog");
const bluetoothTooltips = [
  { anchor: byId("bluetooth-pair-tooltip"), message: byId("bluetooth-pair-message"),
    announcement: byId("bluetooth-pair-announcement") },
  { anchor: byId("bluetooth-scan-tooltip"), message: byId("bluetooth-message"),
    announcement: byId("bluetooth-announcement") },
];
const pairedBluetooth = { list: byId("bluetooth-paired-devices") };
const libraryEditor = {
  dialog: byId("library-dialog"),
  list: byId("library-folders"),
  pending: false,
};
const rekeyDialog = byId("library-rekey-dialog");
const numbers = new Intl.NumberFormat();
let current = null;
let libraryAction = false;
const pendingSettings = new Set();
let libraryError = "";
const playbackEdits = new Map();
const pendingPlayback = new Map();
let playbackWrites = Promise.resolve();
let accentTheme;
let accessEnabled = null;
let wifiTarget = null;
let armedForget = null;
let bluetoothActionFromMain = false;
let requestedPairedDevices = false;
const connectivity = {
  wifi: { list: byId("wifi-networks"), scan: byId("wifi-scan"), message: byId("wifi-message") },
  bluetooth: { list: byId("bluetooth-devices"), scan: byId("bluetooth-scan") },
};

async function request(path, body) {
  const headers = {};
  if (body !== undefined) headers["Content-Type"] = "text/plain";
  const response = await fetch(path, { method: body === undefined ? "GET" : "POST", headers, body, cache: "no-store" });
  const data = await response.json();
  if (!response.ok) {
    const error = new Error(data.error || "DeckDS is unavailable.");
    error.status = response.status;
    throw error;
  }
  return data;
}

function timecode(milliseconds) {
  const seconds = Math.floor(milliseconds / 1000);
  return `${Math.floor(seconds / 60)}:${String(seconds % 60).padStart(2, "0")}`;
}

function showAction(button, alternate, label) {
  if (button.title === label) return;
  button.title = label;
  button.setAttribute("aria-label", label);
  button.firstElementChild.toggleAttribute("hidden", alternate);
  button.lastElementChild.toggleAttribute("hidden", !alternate);
}

function showText(id, value) {
  const field = byId(id);
  if (field.textContent === value) return;
  field.textContent = value;
  field.title = value;
}

function showPlayback(state) {
  const available = Boolean(state?.available);
  const transportBusy = pendingPlayback.has("transport");
  previous.disabled = !available || !state.canPrevious || transportBusy;
  play.disabled = !available || !state.canPlay || transportBusy;
  next.disabled = !available || !state.canNext || transportBusy;
  seek.disabled = !available || !state.trackId || !state.durationMs;
  volume.disabled = !available;
  mute.disabled = !available || pendingPlayback.has("volume");
  if (!state) return;

  showAction(play, state.playing, state.playing ? "Pause" : "Play");
  showAction(mute, state.muted, state.muted ? "Unmute" : "Mute");
  if (!playbackEdits.has("seek") && !pendingPlayback.has("seek")) {
    seek.max = String(state.durationMs || 1);
    seek.value = String(state.positionMs);
    byId("elapsed").textContent = timecode(state.positionMs);
    byId("duration").textContent = timecode(state.durationMs);
  }
  if (!playbackEdits.has("volume") && !pendingPlayback.has("volume")) {
    volume.value = String(state.muted ? 0 : state.volume);
    byId("volume-value").textContent = state.muted ? "Muted" : `${state.volume}%`;
  }
}

function show(state) {
  if (!state.settings) return;
  current = state;
  connection.textContent = "Connected";
  signIn.hidden = true;

  const queued = state.playback?.queued || 0;
  const idleTitle = queued ? `${numbers.format(queued)} ${queued === 1 ? "track" : "tracks"} queued` : "Queue empty";
  showText("track", state.track || idleTitle);
  showText("artist", state.track ? state.artist || "" : "");
  showPlayback(state.playback);
  showText("ota-url", state.ota?.url || "Not set");
  otaEdit.disabled = !state.ota;

  byId("review").hidden = !state.missing;
  byId("missing").textContent = state.removing ? "Removing entries…" :
    `${numbers.format(state.missing)} ${state.missing === 1 ? "file was" : "files were"} not found.`;
  byId("review-question").hidden = state.removing;
  reviewButtons.forEach((button) => { button.disabled = libraryAction || state.removing; });

  if (state.settings.theme !== accentTheme && state.accents?.some(Boolean)) {
    accent.replaceChildren(...state.accents.map((name, index) => new Option(name, index)));
    accentTheme = state.settings.theme;
  }
  settings.forEach((field) => {
    field.disabled = pendingSettings.has(field) ||
      (field === accent && (pendingSettings.has(byId("theme")) || !accent.options.length));
    if (!field.disabled) field.value = String(state.settings[field.dataset.setting]);
  });
  showConnectivity();
  showLibraryConfig();
  showPasswordStatus();
}

async function refresh() {
  const state = await request("/api/state");
  show(state);
  return state;
}

async function poll() {
  try {
    const state = await refresh();
    if (!requestedPairedDevices && state.bluetooth?.supported && !state.bluetooth.busy && !state.bluetooth.scanning) {
      requestedPairedDevices = true;
      networkAction("bluetooth", "paired");
    }
    if (accessEnabled === null) {
      const status = await request("/api/access/password");
      accessEnabled = status.enabled;
      showPasswordStatus();
    }
  } catch (error) {
    current = null;
    signIn.hidden = error.status !== 401;
    connection.textContent = signIn.hidden ? "Reconnecting…" : "Sign-in required";
    showPlayback(null);
    showConnectivity();
    showLibraryConfig();
    showPasswordStatus();
    otaEdit.disabled = true;
    [...scanButtons, ...reviewButtons, ...settings].forEach((field) => { field.disabled = true; });
  } finally {
    setTimeout(poll, 400);
  }
}

function showPasswordStatus() {
  accessEdit.disabled = !current || accessEnabled === null;
  byId("web-password-status").textContent =
    accessEnabled === null ? "" : accessEnabled ? "Enabled" : "Empty";
}

accessEdit.addEventListener("click", () => {
  accessInput.value = "";
  accessError.textContent = "";
  accessDialog.showModal();
});
accessCancel.addEventListener("click", () => accessDialog.close());
accessDialog.addEventListener("cancel", (event) => {
  if (accessSave.disabled) event.preventDefault();
});
accessDialog.addEventListener("close", () => { accessInput.value = ""; });

byId("web-password-form").addEventListener("submit", async (event) => {
  event.preventDefault();
  if (accessSave.disabled) return;
  const password = accessInput.value;
  accessInput.disabled = accessSave.disabled = accessCancel.disabled = true;
  accessError.textContent = "";
  try {
    const status = await request("/api/access/password", password);
    accessEnabled = status.enabled;
    showPasswordStatus();
    accessDialog.close();
  } catch (error) {
    accessError.textContent = error.message;
  } finally {
    accessInput.disabled = accessSave.disabled = accessCancel.disabled = false;
    if (accessDialog.open) accessInput.focus();
  }
});

function deviceButton(label, name, onClick) {
  const button = document.createElement("button");
  button.type = "button";
  button.className = "btn btn-sm btn-outline-secondary";
  button.textContent = label;
  button.setAttribute("aria-label", `${label} ${name}`);
  button.addEventListener("click", onClick);
  return button;
}

function deviceRow(device, detail, actions) {
  const row = document.createElement("li");
  row.className = "device-row";
  const name = document.createElement("div");
  name.className = "device-name small";
  const label = document.createElement("div");
  label.textContent = label.title = device.name;
  const status = document.createElement("div");
  status.className = "text-body-secondary";
  status.textContent = detail;
  name.append(label, status);
  row.append(name);
  if (actions.length) {
    const controls = document.createElement("div");
    controls.className = "device-actions";
    controls.append(...actions);
    row.append(controls);
  }
  return row;
}

function wifiRow(network) {
  const security = { open: "Open", personal: "WPA Personal", sae: "WPA3 Personal", other: "Unsupported security" };
  const detail = network.connected ? "Connected" : `${security[network.security]}, ${network.strength}%`;
  const actions = network.connected || network.security === "other" ? [] :
    [deviceButton("Join", network.name, () => showWifiJoin(network))];
  return deviceRow(network, detail, actions);
}

function bluetoothRow(device) {
  const actions = [];
  if (!device.connected) {
    actions.push(deviceButton(device.paired ? "Connect" : "Pair", device.name,
      () => networkAction("bluetooth", "connect", device.id)));
  }
  if (device.paired) {
    const forget = deviceButton("Forget", device.name, () => {
      if (armedForget?.button === forget) {
        clearForgetConfirmation();
        networkAction("bluetooth", "forget", device.id);
        return;
      }
      clearForgetConfirmation();
      armedForget = { button: forget, name: device.name };
      forget.textContent = "Forget?";
      forget.setAttribute("aria-label", `Confirm forgetting ${device.name}`);
      forget.classList.replace("btn-outline-secondary", "btn-danger");
    });
    forget.classList.add("forget-device");
    forget.addEventListener("blur", () => {
      if (armedForget?.button === forget) clearForgetConfirmation();
    });
    forget.addEventListener("keydown", (event) => {
      if (event.repeat && (event.key === "Enter" || event.key === " ")) event.preventDefault();
    });
    actions.push(forget);
  }
  return deviceRow(device, device.connected ? "Connected" : device.paired ? "Paired" : "Available", actions);
}

function clearForgetConfirmation() {
  if (!armedForget) return;
  const { button, name } = armedForget;
  button.textContent = "Forget";
  button.setAttribute("aria-label", `Forget ${name}`);
  button.classList.replace("btn-danger", "btn-outline-secondary");
  armedForget = null;
}

function showBluetoothTooltips(panel, state) {
  const feedback = panel.error || state?.message || "";
  const target = bluetoothDialog.open ? bluetoothTooltips[1] : bluetoothActionFromMain ? bluetoothTooltips[0] : null;
  for (const tooltip of bluetoothTooltips) {
    const message = tooltip === target ? feedback : "";
    if (tooltip.message.textContent !== message) {
      tooltip.message.textContent = message;
      tooltip.anchor.removeAttribute("data-dismissed");
    }
    if (tooltip.announcement.textContent !== message)
      tooltip.announcement.textContent = message;
    tooltip.anchor.toggleAttribute("data-active", Boolean(message));
    tooltip.message.classList.toggle("text-danger", Boolean(panel.error || state?.error));
  }
}

document.addEventListener("keydown", (event) => {
  if (event.key !== "Escape") return;
  bluetoothTooltips.forEach(({ anchor }) => anchor.setAttribute("data-dismissed", ""));
  clearForgetConfirmation();
});
document.addEventListener("pointerdown", (event) => {
  for (const { anchor } of bluetoothTooltips)
    if (!anchor.contains(event.target)) anchor.setAttribute("data-dismissed", "");
  if (armedForget && !armedForget.button.contains(event.target)) clearForgetConfirmation();
});
for (const { anchor } of bluetoothTooltips) {
  anchor.addEventListener("pointerenter", () => anchor.removeAttribute("data-dismissed"));
  anchor.addEventListener("focusin", () => anchor.removeAttribute("data-dismissed"));
}

function showDeviceList(panel, items, row, busy) {
  const signature = JSON.stringify(items);
  if (items && panel.signature !== signature) {
    panel.list.replaceChildren(...items.map(row));
    panel.signature = signature;
  }
  panel.list.querySelectorAll("button").forEach((button) => { button.disabled = busy || button.hasAttribute("data-unavailable"); });
}

function showConnectivity() {
  const { wifi, bluetooth } = connectivity;
  const wifiState = current?.wifi;
  const wifiBusy = !wifiState?.supported || wifiState.busy || wifi.pending;
  wifi.scan.disabled = Boolean(wifiBusy);
  wifiManual.disabled = Boolean(wifiBusy);
  showDeviceList(wifi, wifiState?.networks, wifiRow, wifiBusy);
  if (wifiState) {
    wifi.message.textContent = wifi.error || wifiState.message;
    wifi.message.classList.toggle("text-danger", Boolean(wifi.error || wifiState.error));
  }
  showWifiSignal(wifiState);

  const bluetoothState = current?.bluetooth;
  const bluetoothBusy = !bluetoothState?.supported || bluetoothState.busy || bluetooth.pending;
  const devices = bluetoothState?.devices;
  bluetooth.scan.setAttribute("aria-disabled", String(Boolean(bluetoothBusy || bluetoothState?.scanning)));
  showDeviceList(pairedBluetooth, devices?.filter((device) => device.paired), bluetoothRow, bluetoothBusy);
  showDeviceList(bluetooth, devices?.filter((device) => !device.paired), bluetoothRow, bluetoothBusy);
  if (armedForget && !armedForget.button.isConnected) clearForgetConfirmation();
  showBluetoothTooltips(bluetooth, bluetoothState);
}

function showWifiSignal(wifi) {
  const accessPoint = wifi?.accessPoint;
  const strength = accessPoint?.strength;
  const hasStrength = Number.isFinite(strength);
  const level = !wifi?.connected ? "off" : !hasStrength || strength >= 67 ? "3" : strength >= 34 ? "2" : "1";
  const name = accessPoint?.name || (wifi?.connected ? "Connected" : "Not connected");
  if (wifiName.textContent !== name) {
    wifiName.textContent = name;
    wifiName.title = name;
  }
  wifiStrength.hidden = !hasStrength;
  const percentage = hasStrength ? `${strength}%` : "";
  if (wifiStrength.textContent !== percentage) wifiStrength.textContent = percentage;
  wifiIcons.forEach((icon) => icon.toggleAttribute("hidden", icon.dataset.level !== level));
}

async function networkAction(name, action, body = "") {
  const panel = connectivity[name];
  if (!current?.[name].supported || panel.pending) return false;
  if (name === "bluetooth" && action !== "paired")
    bluetoothActionFromMain = !bluetoothDialog.open;
  panel.pending = true;
  panel.error = "";
  showConnectivity();
  try {
    await request(`/api/${name}/${action}`, body);
    return true;
  } catch (error) {
    panel.error = error.message;
    return false;
  } finally {
    panel.pending = false;
    showConnectivity();
  }
}
for (const [name, panel] of Object.entries(connectivity)) {
  panel.scan.addEventListener("click", () => {
    if (panel.scan.getAttribute("aria-disabled") !== "true") networkAction(name, "scan");
  });
}

function showWifiJoin(network = null) {
  wifiTarget = network || { manual: true };
  const name = byId("wifi-target");
  name.hidden = !network;
  name.textContent = network?.name || "";
  byId("wifi-ssid-field").hidden = Boolean(network);
  wifiSsid.required = !network;
  wifiSsid.value = "";
  wifiPassword.required = Boolean(network && network.security !== "open");
  byId("wifi-password-field").hidden = Boolean(network && !wifiPassword.required);
  wifiPassword.value = "";
  wifiJoinError.textContent = "";
  wifiBrowser.hidden = true;
  wifiForm.hidden = false;
  wifiDialog.setAttribute("aria-labelledby", "wifi-join-title");
  (network ? (wifiPassword.required ? wifiPassword : wifiJoin) : wifiSsid).focus();
}

function showWifiNetworks() {
  wifiPassword.value = "";
  wifiSsid.value = "";
  wifiSsid.required = false;
  wifiTarget = null;
  wifiForm.hidden = true;
  wifiBrowser.hidden = false;
  wifiDialog.setAttribute("aria-labelledby", "wifi-dialog-title");
  if (wifiDialog.open) wifiClose.focus();
}

byId("wifi-open").addEventListener("click", () => {
  showWifiNetworks();
  wifiDialog.showModal();
});
wifiManual.addEventListener("click", () => showWifiJoin());
wifiClose.addEventListener("click", () => wifiDialog.close());
wifiCancel.addEventListener("click", showWifiNetworks);
wifiDialog.addEventListener("cancel", (event) => {
  if (wifiTarget) {
    event.preventDefault();
    if (!wifiJoin.disabled) showWifiNetworks();
  }
});
wifiDialog.addEventListener("close", showWifiNetworks);
wifiForm.addEventListener("submit", async (event) => {
  event.preventDefault();
  if (!wifiTarget || wifiJoin.disabled) return;
  wifiJoinError.textContent = "";
  if (wifiTarget.manual && new TextEncoder().encode(wifiSsid.value).length > 32) {
    wifiJoinError.textContent = "Enter a network name of up to 32 bytes.";
    return;
  }
  wifiJoin.disabled = wifiCancel.disabled = wifiPassword.disabled = wifiSsid.disabled = true;
  const action = wifiTarget.manual ? "manual" : "connect";
  const target = wifiTarget.manual ? wifiSsid.value : wifiTarget.id;
  const joined = await networkAction("wifi", action, `${target}\n${wifiPassword.value}`);
  if (joined) showWifiNetworks();
  else wifiJoinError.textContent = connectivity.wifi.error || "Wi-Fi is unavailable.";
  wifiJoin.disabled = wifiCancel.disabled = wifiPassword.disabled = wifiSsid.disabled = false;
});

byId("bluetooth-open").addEventListener("click", () => {
  bluetoothTooltips[1].anchor.removeAttribute("data-dismissed");
  bluetoothDialog.showModal();
  showConnectivity();
});
byId("bluetooth-close").addEventListener("click", () => bluetoothDialog.close());
bluetoothDialog.addEventListener("close", showConnectivity);

function libraryFolderRow(folder) {
  const sync = deviceButton("Sync", folder.identifier, null);
  sync.setAttribute("data-unavailable", "");
  const row = deviceRow({ name: folder.identifier }, folder.root, [
    deviceButton("Edit", folder.identifier, () => editLibraryFolder("rename", folder)),
    sync,
  ]);
  row.classList.add("library-row");
  const title = row.firstElementChild.firstElementChild;
  title.classList.add("library-row-title");
  const name = document.createElement("span");
  name.className = "text-truncate";
  name.textContent = folder.identifier;
  const namespace = document.createElement("span");
  namespace.className = "library-row-namespace text-truncate";
  namespace.textContent = `Namespace ${folder.namespace || "—"}`;
  namespace.title = folder.namespace;
  title.replaceChildren(name, namespace);
  if (folder.enabled === false) {
    const path = title.nextElementSibling;
    path.classList.add("d-flex", "gap-2");
    const location = document.createElement("span");
    location.className = "text-truncate";
    location.textContent = folder.root;
    const status = document.createElement("span");
    status.className = "flex-shrink-0";
    status.textContent = "Disabled";
    path.replaceChildren(location, status);
  }
  return row;
}

function editedLibrary() {
  return current?.libraryConfig?.folders?.find((folder) => folder.id === libraryEditor.folder?.id);
}

function showLibraryConfig() {
  const state = current?.libraryConfig;
  const busy = !state?.supported || state.busy || libraryEditor.pending || libraryAction;
  const folders = state?.folders;
  byId("library-add").disabled = busy;
  byId("library-rekey").disabled = busy || !folders?.length;
  byId("library-rekey-confirm").disabled = busy || !folders?.length;
  byId("library-edit-save").disabled = busy;
  const edited = editedLibrary();
  const disabled = edited?.enabled === false;
  byId("library-toggle").textContent = disabled ? "Enable" : "Disable";
  byId("library-toggle").disabled = busy || !edited;
  byId("library-delete").disabled = byId("library-delete-confirm").disabled = busy || !disabled;
  byId("library-apply").disabled = busy || !state?.preview || state.preview.token !== libraryEditor.token;
  for (const id of ["library-edit-back", "library-preview-back", "library-rekey-cancel", "library-delete-back"])
    byId(id).disabled = libraryEditor.pending;
  showDeviceList(libraryEditor, folders, libraryFolderRow, busy);
  if (folders) {
    libraryEditor.list.hidden = folders.length === 0;
    byId("folder").hidden = folders.length !== 0;
  }
  scanButtons.forEach((button) => { button.disabled = libraryAction || libraryEditor.pending || !current?.canScan; });
  if (!current) return;

  const progress = state?.rekey || current.scan;
  const error = !progress && (libraryEditor.error || libraryError || state?.error);
  const tracks = byId("tracks");
  tracks.textContent = `Ready, ${numbers.format(current.tracks)} ${current.tracks === 1 ? "track" : "tracks"}`;
  tracks.hidden = Boolean(progress || error);
  byId("scan").hidden = !progress;
  const feedback = byId("library-message");
  feedback.classList.toggle("visually-hidden", !error);
  feedback.classList.toggle("text-danger", Boolean(error));
  if (progress) {
    const { checked, total, full } = progress;
    const action = state?.rekey ? "ReKey" : full ? "Full Scan" : "Quick Scan";
    const percent = total ? Math.min(99, Math.floor(100 * checked / total)) : 0;
    byId("percent").textContent = `${percent}%`;
    byId("bar").value = percent;
    byId("bar").setAttribute("aria-label", `${action} progress`);
    byId("bar").setAttribute("aria-valuetext", total ? `${numbers.format(checked)} of ${numbers.format(total)} tracks` : "Preparing");
    showText("library-message", "");
  } else if (!libraryAction) {
    const message = error || state?.message || current.message || "";
    showText("library-message", message);
    tracks.title = state?.backup && !error ? `${message} Backup at ${state.backup}` : message;
  }
}

function showLibraryView(view, title) {
  const views = { edit: "library-edit-form", preview: "library-preview-view", delete: "library-delete-view" };
  const heading = `library-${view}-heading`;
  for (const [name, id] of Object.entries(views)) byId(id).hidden = name !== view;
  byId(heading).textContent = title;
  libraryEditor.dialog.setAttribute("aria-labelledby", heading);
  showLibraryConfig();
}

function editLibraryFolder(action, folder) {
  const path = byId("library-path");
  const identifier = byId("library-identifier");
  const syncUrl = byId("library-sync-url");
  const add = action === "add";
  libraryEditor.action = action;
  libraryEditor.folder = folder;
  byId("library-path-field").hidden = !add;
  byId("library-relocate-field").hidden = add;
  byId("library-state-actions").hidden = add;
  path.value = "";
  path.required = add;
  identifier.value = folder?.identifier || "";
  identifier.required = !add;
  syncUrl.value = folder?.syncUrl || "";
  byId("library-relocate-path").value = "";
  byId("library-edit-message").textContent = "";
  byId("library-edit-save").textContent = add ? "Add" : "Save";
  showLibraryView("edit", add ? "Add Library" : "Edit Library");
  libraryEditor.dialog.showModal();
  (add ? path : identifier).focus();
}

async function runLibraryAction(action, body = "", onAccepted) {
  if (libraryEditor.pending || libraryAction || !current?.libraryConfig?.supported || current.libraryConfig.busy)
    throw new Error("Wait for the current library operation to finish.");
  const { revision } = current.libraryConfig;
  libraryEditor.error = libraryError = "";
  libraryEditor.pending = true;
  showLibraryConfig();
  try {
    await request(`/api/library/manage/${action}`, body);
    onAccepted?.();
    let inactivePolls = 0;
    while (inactivePolls < 10) {
      const { libraryConfig: result } = await refresh();
      if (result.revision !== revision) {
        if (result.error) throw new Error(result.error);
        return result;
      }
      inactivePolls = result.busy ? 0 : inactivePolls + 1;
      await new Promise((resolve) => setTimeout(resolve, 300));
    }
    throw new Error("DeckDS has not confirmed the library operation.");
  } finally {
    libraryEditor.pending = false;
    showLibraryConfig();
  }
}

function showRelocationPreview(result) {
  const preview = result.preview;
  if (!preview) throw new Error("The library preview is unavailable.");
  libraryEditor.token = preview.token;
  byId("library-preview-items").replaceChildren(...preview.unmatched.map((item) => {
    const row = document.createElement("li");
    row.textContent = item;
    return row;
  }));
  const count = (value, one, many = `${one}s`) => `${numbers.format(value)} ${value === 1 ? one : many}`;
  byId("library-preview-summary").textContent =
    `${count(preview.matched, "file")} matched. ${count(preview.missing, "unmatched entry", "unmatched entries")} will be kept for review.`;
  showText("library-preview-destination", preview.destination);
  const report = byId("library-preview-report");
  report.href = `/api/library/unmatched?preview=${preview.token}`;
  report.hidden = !preview.missing;
  byId("library-preview-message").textContent = "";
  byId("library-apply").textContent = "Relocate";
  showLibraryView("preview", "Preview Relocation");
  byId("library-preview-back").focus();
}

byId("library-add").addEventListener("click", () => editLibraryFolder("add", null));
byId("library-edit-back").addEventListener("click", () => libraryEditor.dialog.close());
byId("library-preview-back").addEventListener("click", () => {
  showLibraryView("edit", "Edit Library");
  byId("library-relocate-path").focus();
});
byId("library-toggle").addEventListener("click", async () => {
  const folder = editedLibrary();
  if (!folder) return;
  showText("library-edit-message", "");
  try {
    await runLibraryAction(folder.enabled === false ? "enable" : "disable", folder.id);
  } catch (error) {
    showText("library-edit-message", error.message);
  }
});
byId("library-delete").addEventListener("click", () => {
  const folder = editedLibrary();
  if (folder?.enabled !== false) return;
  showText("library-delete-name", folder.identifier);
  showText("library-delete-message", "");
  showLibraryView("delete", "Delete Library?");
  byId("library-delete-back").focus();
});
byId("library-delete-back").addEventListener("click", () => {
  showLibraryView("edit", "Edit Library");
  byId("library-delete").focus();
});
byId("library-delete-confirm").addEventListener("click", async () => {
  const folder = editedLibrary();
  if (folder?.enabled !== false) return;
  try {
    await runLibraryAction("delete", folder.id);
    libraryEditor.dialog.close();
  } catch (error) {
    showText("library-delete-message", error.message);
  }
});
for (const dialog of [libraryEditor.dialog, rekeyDialog]) {
  dialog.addEventListener("cancel", (event) => {
    if (libraryEditor.pending) event.preventDefault();
  });
}
byId("library-rekey").addEventListener("click", () => {
  showText("library-rekey-error", "");
  rekeyDialog.showModal();
});
byId("library-rekey-cancel").addEventListener("click", () => rekeyDialog.close());
byId("library-rekey-confirm").addEventListener("click", async () => {
  showText("library-rekey-error", "");
  try {
    await runLibraryAction("rekey", "", () => rekeyDialog.close());
  } catch (error) {
    if (rekeyDialog.open) showText("library-rekey-error", error.message);
    else {
      libraryEditor.error = error.message;
      showLibraryConfig();
    }
  }
});
byId("library-edit-form").addEventListener("submit", async (event) => {
  event.preventDefault();
  const { action, folder } = libraryEditor;
  const add = action === "add";
  const destination = byId("library-relocate-path").value;
  const relocate = !add && destination.trim() !== "";
  const values = [add ? byId("library-path").value : folder.id];
  showText("library-edit-message", "");
  const url = networkUrl(byId("library-sync-url").value);
  if (url === null) {
    showText("library-edit-message", "Enter a Sync URL with a server address.");
    return;
  }
  if (relocate) values.push(destination);
  values.push(byId("library-identifier").value, url);
  try {
    const result = await runLibraryAction(relocate ? "relocate-preview" : action, values.join("\n"));
    if (relocate) showRelocationPreview(result);
    else libraryEditor.dialog.close();
  } catch (error) {
    showText("library-edit-message", error.message);
  }
});
byId("library-apply").addEventListener("click", async () => {
  try {
    await runLibraryAction("apply", String(libraryEditor.token));
    libraryEditor.dialog.close();
  } catch (error) {
    showText("library-preview-message", error.message);
  }
});

function networkUrl(value, httpOnly = false) {
  const raw = value.trim();
  if (!raw) return "";
  try {
    const url = new URL(raw);
    if (url.hostname && (!httpOnly || url.protocol === "http:" || url.protocol === "https:")) return url.href;
  } catch {
    // The field error also covers invalid URLs.
  }
  return null;
}

function bindOtaUrlEditor() {
  const dialog = byId("ota-dialog");
  const input = byId("ota-input");
  const save = byId("ota-save");
  const cancel = byId("ota-cancel");
  const feedback = byId("ota-error");

  otaEdit.addEventListener("click", () => {
    input.value = current.ota.url;
    feedback.textContent = "";
    dialog.showModal();
  });
  cancel.addEventListener("click", () => dialog.close());
  dialog.addEventListener("cancel", (event) => {
    if (save.disabled) event.preventDefault();
  });

  byId("ota-form").addEventListener("submit", async (event) => {
    event.preventDefault();
    if (save.disabled) return;
    feedback.textContent = "";
    const url = networkUrl(input.value, true);
    if (url === null) {
      feedback.textContent = "Enter a valid HTTP or HTTPS URL.";
      return;
    }
    if (!current?.ota) {
      feedback.textContent = "DeckDS is unavailable.";
      return;
    }
    const { revision } = current.ota;
    input.disabled = save.disabled = cancel.disabled = true;
    try {
      await request("/api/ota/url", url);
      for (let attempt = 0; attempt < 20; attempt++) {
        const setting = (await refresh()).ota;
        if (setting.revision !== revision) {
          if (setting.error) throw new Error(setting.error);
          if (setting.url !== url) throw new Error("The saved OTA URL changed. Try again.");
          dialog.close();
          return;
        }
        await new Promise((resolve) => setTimeout(resolve, 150));
      }
      throw new Error("DeckDS has not confirmed the save. Try again.");
    } catch (error) {
      feedback.textContent = error.message;
    } finally {
      input.disabled = save.disabled = cancel.disabled = false;
      if (dialog.open) input.focus();
    }
  });
}

bindOtaUrlEditor();

async function changePlayback(action, body = "", group = "transport") {
  if (!current?.playback?.available) return;
  const change = {};
  pendingPlayback.set(group, change);
  byId("playback-error").textContent = "";
  showPlayback(current.playback);
  const write = playbackWrites.then(() => {
    if (pendingPlayback.get(group) === change)
      return request(`/api/playback/${action}`, body);
  });
  playbackWrites = write.catch(() => {});

  let error = "";
  try {
    await write;
    await new Promise((resolve) => setTimeout(resolve, 200));
  } catch (failure) {
    error = failure.message;
  }
  if (pendingPlayback.get(group) !== change) return;
  pendingPlayback.delete(group);
  try {
    await refresh();
  } catch {
    showPlayback(null);
  }
  byId("playback-error").textContent = error;
}

function bindPlaybackRange(field, group, showValue, payload) {
  const begin = () => {
    if (!playbackEdits.has(group))
      playbackEdits.set(group, current?.playback?.trackId || "");
  };
  const finish = () => playbackEdits.delete(group);
  field.addEventListener("pointerdown", begin);
  field.addEventListener("input", () => {
    begin();
    showValue(field.value);
  });
  field.addEventListener("change", () => {
    const trackId = playbackEdits.get(group) ?? current?.playback?.trackId;
    finish();
    const body = payload(field.value, trackId);
    if (body !== null) changePlayback(group, body, group);
  });
  field.addEventListener("pointerup", () => setTimeout(finish, 0));
  field.addEventListener("pointercancel", finish);
  field.addEventListener("blur", finish);
}

previous.addEventListener("click", () => changePlayback("previous"));
play.addEventListener("click", () => changePlayback(current?.playback?.playing ? "pause" : "play"));
next.addEventListener("click", () => changePlayback("next"));
mute.addEventListener("click", () => changePlayback("mute", current?.playback?.muted ? "0" : "1", "volume"));
bindPlaybackRange(seek, "seek", (value) => { byId("elapsed").textContent = timecode(Number(value)); },
  (value, trackId) => trackId ? `${trackId}:${value}` : null);
bindPlaybackRange(volume, "volume", (value) => { byId("volume-value").textContent = `${value}%`; }, (value) => value);

async function changeLibrary(path, body) {
  if (libraryAction) return;
  libraryAction = true;
  libraryEditor.error = libraryError = "";
  showLibraryConfig();
  [...scanButtons, ...reviewButtons].forEach((button) => { button.disabled = true; });
  try {
    await request(path, body);
    await new Promise((resolve) => setTimeout(resolve, 250));
    libraryAction = false;
    await refresh();
  } catch (error) {
    libraryAction = false;
    libraryError = error.message;
    if (current) show(current);
  }
}

scanButtons.forEach((button) => {
  button.addEventListener("click", () => changeLibrary("/api/scan", button.id));
});
reviewButtons.forEach((button) => {
  button.addEventListener("click", () => {
    if (current && current.missing) changeLibrary("/api/missing", `${current.review}:${button.id}`);
  });
});

settings.forEach((field) => {
  field.addEventListener("change", async () => {
    const value = field === accent ? `${accentTheme}:${field.value}` : field.value;
    pendingSettings.add(field);
    field.disabled = true;
    if (field === byId("theme")) accent.disabled = true;
    byId("settings-error").textContent = "";
    try {
      await request(`/api/settings/${field.dataset.setting}`, value);
      await new Promise((resolve) => setTimeout(resolve, 250));
      pendingSettings.delete(field);
      await refresh();
    } catch (error) {
      pendingSettings.delete(field);
      if (current) show(current);
      byId("settings-error").textContent = error.message;
    }
  });
});

poll();
