const byId = (id) => document.getElementById(id);
const connection = byId("connection");
const scanButtons = [byId("quick"), byId("full")];
const reviewButtons = [byId("keep"), byId("remove")];
const settings = [...document.querySelectorAll("select[data-setting]")];
const numbers = new Intl.NumberFormat();
let current = null;
let libraryAction = false;
const pendingSettings = new Set();
let libraryError = "";

async function request(path, body) {
  const headers = {};
  if (body !== undefined) headers["Content-Type"] = "text/plain";
  const response = await fetch(path, { method: body === undefined ? "GET" : "POST", headers, body, cache: "no-store" });
  const data = await response.json();
  if (!response.ok) {
    throw new Error(data.error || "DeckDS is unavailable.");
  }
  return data;
}

function show(state) {
  if (!state.settings) return;
  current = state;
  connection.textContent = "Connected";

  byId("tracks").textContent = `${numbers.format(state.tracks)} ${state.tracks === 1 ? "track" : "tracks"}`;
  byId("folder").textContent = state.folder || "Choose a music folder in DeckDS.";
  byId("track").textContent = state.track || "No track selected";
  byId("artist").textContent = state.artist;
  byId("scan").hidden = !state.scan;
  if (state.scan) {
    const { checked, total, full } = state.scan;
    const percent = total ? Math.min(99, Math.floor(100 * checked / total)) : 0;
    byId("scan-mode").textContent = full ? "Full Scan" : "Quick Scan";
    byId("percent").textContent = `${percent}%`;
    byId("bar").value = percent;
    byId("count").textContent = `${numbers.format(checked)} / ${total ? numbers.format(total) : "?"}`;
  }

  byId("review").hidden = !state.missing;
  byId("missing").textContent = state.removing ? "Removing entries…" :
    `${numbers.format(state.missing)} ${state.missing === 1 ? "file was" : "files were"} not found.`;
  byId("review-question").hidden = state.removing;
  scanButtons.forEach((button) => { button.disabled = libraryAction || !state.canScan; });
  reviewButtons.forEach((button) => { button.disabled = libraryAction || state.removing; });

  settings.forEach((field) => {
    field.disabled = pendingSettings.has(field);
    if (!field.disabled) field.value = String(state.settings[field.dataset.setting]);
  });
  if (!libraryAction) byId("library-message").textContent = libraryError || state.message;
}

async function refresh() {
  show(await request("/api/state"));
}

async function poll() {
  try {
    await refresh();
  } catch {
    current = null;
    connection.textContent = "Reconnecting…";
    [...scanButtons, ...reviewButtons, ...settings].forEach((field) => { field.disabled = true; });
  } finally {
    setTimeout(poll, 400);
  }
}

async function changeLibrary(path, body) {
  if (libraryAction) return;
  libraryAction = true;
  libraryError = "";
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
    pendingSettings.add(field);
    field.disabled = true;
    byId("settings-error").textContent = "";
    try {
      await request(`/api/settings/${field.dataset.setting}`, field.value);
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
