"use strict";

// ============================================================
// State
// ============================================================

const state = {
  route: "dashboard",
  boardPins: [],
  sensorsConfig: [],
  sensorsLive: [],
  ioConfig: [],
  ioLive: [],
  uptimeMs: 0,
  timeAnchor: null,
  helpTopic: "rest-api",
};

const HELP_TOPICS = ["rest-api", "mqtt", "prtg"];

const sensorDialogState = { selectedGpio: null };
const ioDialogState = { selectedGpio: null };
let dashInterval = null;

// Kuratierte Zeitzonen-Auswahl (POSIX-TZ-Strings) fuer das Uhrzeit-Formular.
// Deckt gaengige Regionen ab; alles darueber hinaus geht ueber die
// "Benutzerdefiniert"-Option (freier POSIX-TZ-String).
const TIMEZONES = [
  { value: "UTC0", label: "UTC" },
  { value: "GMT0BST,M3.5.0/1,M10.5.0", label: "UTC+0/+1 – London" },
  { value: "CET-1CEST,M3.5.0,M10.5.0/3", label: "UTC+1/+2 – Berlin, Paris, Madrid, Rom, Amsterdam, Wien" },
  { value: "EET-2EEST,M3.5.0/3,M10.5.0/4", label: "UTC+2/+3 – Helsinki, Athen, Kiew, Bukarest" },
  { value: "MSK-3", label: "UTC+3 – Moskau" },
  { value: "<-03>3", label: "UTC-3 – São Paulo" },
  { value: "EST5EDT,M3.2.0,M11.1.0", label: "UTC-5/-4 – New York, Toronto" },
  { value: "CST6CDT,M3.2.0,M11.1.0", label: "UTC-6/-5 – Chicago" },
  { value: "MST7MDT,M3.2.0,M11.1.0", label: "UTC-7/-6 – Denver" },
  { value: "PST8PDT,M3.2.0,M11.1.0", label: "UTC-8/-7 – Los Angeles, Vancouver" },
  { value: "IST-5:30", label: "UTC+5:30 – Neu-Delhi, Mumbai" },
  { value: "<+04>-4", label: "UTC+4 – Dubai" },
  { value: "<+07>-7", label: "UTC+7 – Bangkok, Jakarta" },
  { value: "CST-8", label: "UTC+8 – Peking, Shanghai, Singapur" },
  { value: "JST-9", label: "UTC+9 – Tokio, Seoul" },
  { value: "AEST-10AEDT,M10.1.0,M4.1.0/3", label: "UTC+10/+11 – Sydney, Melbourne" },
  { value: "NZST-12NZDT,M9.5.0,M4.1.0/3", label: "UTC+12/+13 – Auckland" },
  { value: "SAST-2", label: "UTC+2 – Johannesburg" },
];

// ============================================================
// API-Client
// ============================================================

const api = {
  async request(method, path, body) {
    const opts = { method, headers: {} };
    if (body !== undefined) {
      opts.headers["Content-Type"] = "application/json";
      opts.body = JSON.stringify(body);
    }
    const res = await fetch(path, opts);
    if (res.status === 401) {
      throw new Error(t("toast.auth.required"));
    }
    if (!res.ok) {
      let msg = `HTTP ${res.status}`;
      try {
        const text = await res.text();
        if (text && text.length < 200 && !text.trim().startsWith("<")) {
          msg = text;
        }
      } catch (e) { /* ignore */ }
      throw new Error(msg);
    }
    const ct = res.headers.get("content-type") || "";
    if (ct.includes("application/json")) {
      return res.json();
    }
    return null;
  },
  get(path) { return api.request("GET", path); },
  put(path, body) { return api.request("PUT", path, body); },
  post(path, body) { return api.request("POST", path, body === undefined ? {} : body); },
};

// ============================================================
// Helfer
// ============================================================

function escapeHtml(s) {
  return String(s ?? "").replace(/[&<>"']/g, (c) => (
    { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]
  ));
}

function typeLabel(t) {
  return { dallas: "Dallas", dht11: "DHT11", am2301: "AM2301" }[t] || t;
}

function fieldLabel(f) {
  return f === "humidity_pct" ? t("field.short.humidity") : t("field.short.temp");
}

function opLabel(o) {
  return { gt: ">", gte: "≥", lt: "<", lte: "≤" }[o] || o;
}

function ruleSummary(rule) {
  const base = `${fieldLabel(rule.field)} ${opLabel(rule.operator)} ${rule.threshold}`;
  return rule.hysteresis > 0 ? `${base} ${t("rule.hysteresis.suffix", { n: rule.hysteresis })}` : base;
}

function formatUptime(s) {
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  if (h > 0) return t("uptime.hm", { h, m });
  return t("uptime.m", { m });
}

function ageString(lastMs) {
  if (!lastMs || !state.uptimeMs) return t("age.no.reading");
  const diffS = Math.max(0, Math.round((state.uptimeMs - lastMs) / 1000));
  if (diffS < 5) return t("age.just.now");
  if (diffS < 60) return t("age.seconds.ago", { n: diffS });
  if (diffS < 3600) return t("age.minutes.ago", { n: Math.round(diffS / 60) });
  return t("age.hours.ago", { n: Math.round(diffS / 3600) });
}

function makeId(label, existingIds) {
  const map = { "ä": "ae", "ö": "oe", "ü": "ue", "ß": "ss", "Ä": "Ae", "Ö": "Oe", "Ü": "Ue" };
  let base = String(label || "item").replace(/[äöüßÄÖÜ]/g, (c) => map[c] || c)
    .toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/(^-+|-+$)/g, "");
  if (!base) base = "item";
  let id = base;
  let i = 1;
  while (existingIds.includes(id)) {
    id = `${base}-${i++}`;
  }
  return id;
}

function toast(msg, kind) {
  const container = document.getElementById("toasts");
  const el = document.createElement("div");
  el.className = "toast" + (kind === "error" ? " error" : kind === "success" ? " success" : "");
  el.textContent = msg;
  container.appendChild(el);
  setTimeout(() => el.remove(), 4500);
}

function confirmDialog(title, message) {
  return new Promise((resolve) => {
    document.getElementById("confirm-title").textContent = title;
    document.getElementById("confirm-message").textContent = message;
    const dlg = document.getElementById("dialog-confirm");
    const okBtn = document.getElementById("confirm-ok");
    let result = false;
    const onOk = () => { result = true; dlg.close(); };
    const onClose = () => {
      okBtn.removeEventListener("click", onOk);
      dlg.removeEventListener("close", onClose);
      resolve(result);
    };
    okBtn.addEventListener("click", onOk);
    dlg.addEventListener("close", onClose);
    dlg.showModal();
  });
}

function renderPinGrid(container, opts) {
  const { selected, taken, onSelect } = opts;
  container.innerHTML = "";
  const pins = [...state.boardPins].sort((a, b) => a.gpio - b.gpio);
  for (const p of pins) {
    const isTaken = taken.has(p.gpio) && p.gpio !== selected;
    const chip = document.createElement("div");
    chip.className = "pin-chip"
      + (p.gpio === selected ? " selected" : "")
      + (isTaken ? " taken" : "")
      + (p.note ? " reserved-note" : "");
    chip.textContent = "GPIO" + p.gpio;
    chip.title = p.note || t("pin.free");
    if (!isTaken) {
      chip.addEventListener("click", () => onSelect(p.gpio));
    }
    container.appendChild(chip);
  }
}

// ============================================================
// Theme
// ============================================================

function applyTheme(theme) {
  if (theme) {
    document.documentElement.setAttribute("data-theme", theme);
    localStorage.setItem("tw-theme", theme);
  } else {
    document.documentElement.removeAttribute("data-theme");
    localStorage.removeItem("tw-theme");
  }
}

// ============================================================
// Sprache
// ============================================================

function updateLangToggleLabel() {
  document.getElementById("lang-toggle-label").textContent = currentLang.toUpperCase();
}

function setLang(lang) {
  if (!SUPPORTED_LANGS.includes(lang)) return;
  currentLang = lang;
  localStorage.setItem("tw-lang", lang);
  updateLangToggleLabel();
  applyStaticTranslations();
  document.getElementById("page-title").textContent = t("nav." + state.route);
  onRouteEnter(state.route);
}

// ============================================================
// Routing
// ============================================================

const ROUTES = ["dashboard", "sensors", "io", "network", "mqtt", "snmp", "system", "help"];

function setRoute(route) {
  if (!ROUTES.includes(route)) route = "dashboard";
  state.route = route;
  document.querySelectorAll(".section").forEach((s) => s.classList.remove("active"));
  document.getElementById("section-" + route).classList.add("active");
  document.querySelectorAll(".nav__item").forEach((b) => b.classList.toggle("active", b.dataset.route === route));
  document.getElementById("page-title").textContent = t("nav." + route);
  if (location.hash.slice(1) !== route) location.hash = route;
  document.getElementById("sidebar").classList.remove("open");
  onRouteEnter(route);
}

async function onRouteEnter(route) {
  if (route === "dashboard") {
    await refreshDashboard();
    startDashboardPolling();
  } else {
    stopDashboardPolling();
  }

  try {
    if (route === "sensors" || route === "io") {
      await Promise.all([loadSensors(), loadIo()]);
    }
    if (route === "sensors") renderSensorsTable();
    if (route === "io") renderIoTable();
    if (route === "network") await Promise.all([loadNetworkForm(), loadTimeForm()]);
    if (route === "mqtt") await loadMqttForm();
    if (route === "snmp") await loadSnmpForm();
    if (route === "system") await Promise.all([loadSystemInfo(), loadAuthForm()]);
    if (route === "help") await loadHelpTopic(state.helpTopic);
  } catch (err) {
    toast(t("toast.load.error", { msg: err.message }), "error");
  }
}

function startDashboardPolling() {
  if (dashInterval) return;
  dashInterval = setInterval(refreshDashboard, 4000);
}

function stopDashboardPolling() {
  if (dashInterval) {
    clearInterval(dashInterval);
    dashInterval = null;
  }
}

// ============================================================
// Dashboard
// ============================================================

async function refreshDashboard() {
  try {
    const [sensors, io, info] = await Promise.all([
      api.get("/api/sensors"), api.get("/api/io"), api.get("/api/system/info"),
    ]);
    state.sensorsLive = sensors;
    state.ioLive = io;
    state.uptimeMs = info.uptime_s * 1000;
    renderDashboardSensors();
    renderDashboardIo();
  } catch (err) {
    if (state.route === "dashboard") toast(t("toast.refresh.error", { msg: err.message }), "error");
  }
}

function sensorCard(s) {
  const ok = s.has_reading && s.last_read_ok;
  const tempStr = ok && s.temperature_c !== undefined ? s.temperature_c.toFixed(1) + "°C" : "–";
  const humStr = ok && s.humidity_pct !== undefined ? s.humidity_pct.toFixed(0) + "% rF" : null;
  const badge = !s.has_reading
    ? `<span class="badge"><span class="dot"></span>${t("badge.waiting")}</span>`
    : ok ? `<span class="badge ok"><span class="dot"></span>${t("badge.ok")}</span>` : `<span class="badge danger"><span class="dot"></span>${t("badge.error")}</span>`;
  return `
    <div class="card">
      <div class="card__header">
        <div>
          <div class="card__subtitle">${typeLabel(s.type)} &middot; GPIO${s.gpio}</div>
          <div class="card__title">${escapeHtml(s.label || s.id)}</div>
        </div>
        ${badge}
      </div>
      <div class="card__value">${tempStr} ${humStr ? `<small>${humStr}</small>` : ""}</div>
      <div class="card__foot">${escapeHtml(ageString(s.last_read_time_ms))}</div>
    </div>`;
}

function ioCard(io) {
  const hasRule = !!io.rule;
  let body;
  if (io.type === "output") {
    body = hasRule
      ? `<div class="card__foot">${t("io.auto", { rule: ruleSummary(io.rule) })}</div>`
      : `<button class="btn btn--sm" style="margin-top:.5rem" data-toggle-io="${io.id}" data-next-state="${io.state ? "0" : "1"}">${io.state ? t("io.turn.off") : t("io.turn.on")}</button>`;
  } else {
    body = `<div class="card__foot">${t("io.digital.input")}</div>`;
  }
  return `
    <div class="card">
      <div class="card__header">
        <div>
          <div class="card__subtitle">${io.type === "output" ? t("io.output") : t("io.input")} &middot; GPIO${io.gpio}</div>
          <div class="card__title">${escapeHtml(io.label || io.id)}</div>
        </div>
        <span class="badge ${io.state ? "ok" : ""}"><span class="dot"></span>${io.state ? t("io.state.on") : t("io.state.off")}</span>
      </div>
      ${body}
    </div>`;
}

function renderDashboardSensors() {
  const el = document.getElementById("dash-sensors");
  if (!state.sensorsLive.length) {
    el.innerHTML = `<div class="empty-state"><strong>${t("dashboard.sensors.empty.title")}</strong>${t("dashboard.sensors.empty.hint")}</div>`;
    return;
  }
  el.innerHTML = state.sensorsLive.map(sensorCard).join("");
}

function renderDashboardIo() {
  const el = document.getElementById("dash-io");
  if (!state.ioLive.length) {
    el.innerHTML = `<div class="empty-state"><strong>${t("dashboard.io.empty.title")}</strong>${t("dashboard.io.empty.hint")}</div>`;
    return;
  }
  el.innerHTML = state.ioLive.map(ioCard).join("");
  el.querySelectorAll("[data-toggle-io]").forEach((btn) => {
    btn.addEventListener("click", () => toggleIo(btn.dataset.toggleIo, btn.dataset.nextState === "1"));
  });
}

async function toggleIo(id, next) {
  try {
    await api.post(`/api/io/set?id=${encodeURIComponent(id)}`, { state: next });
    await refreshDashboard();
    if (state.route === "io") {
      await loadIo();
      renderIoTable();
    }
  } catch (err) {
    toast(t("toast.toggle.error", { msg: err.message }), "error");
  }
}

// ============================================================
// Sensoren
// ============================================================

async function loadSensors() {
  const [config, live] = await Promise.all([api.get("/api/sensors/config"), api.get("/api/sensors")]);
  state.sensorsConfig = config;
  state.sensorsLive = live;
}

function renderSensorsTable() {
  const el = document.getElementById("sensors-table");
  if (!state.sensorsConfig.length) {
    el.innerHTML = `<div class="empty-state"><strong>${t("sensors.empty.title")}</strong>${t("sensors.empty.hint")}</div>`;
    return;
  }
  const rows = state.sensorsConfig.map((s) => {
    const live = state.sensorsLive.find((l) => l.id === s.id);
    const statusBadge = !live || !live.has_reading
      ? `<span class="badge">${t("badge.waiting")}</span>`
      : live.last_read_ok ? `<span class="badge ok"><span class="dot"></span>${t("badge.ok")}</span>` : `<span class="badge danger"><span class="dot"></span>${t("badge.error")}</span>`;
    return `<tr>
      <td>${escapeHtml(s.label || s.id)}</td>
      <td>${typeLabel(s.type)}</td>
      <td class="mono">GPIO${s.gpio}</td>
      <td class="mono">${s.rom_id ? escapeHtml(s.rom_id) : "–"}</td>
      <td>${s.poll_interval_s}s</td>
      <td>${statusBadge}</td>
      <td class="table-actions">
        <button class="btn btn--sm" data-edit-sensor="${s.id}">${t("btn.edit")}</button>
        <button class="btn btn--sm btn--danger" data-delete-sensor="${s.id}">${t("btn.delete")}</button>
      </td>
    </tr>`;
  }).join("");
  el.innerHTML = `<div class="table-wrap"><table>
    <thead><tr><th>${t("table.label")}</th><th>${t("table.type")}</th><th>${t("table.gpio")}</th><th>${t("table.romid")}</th><th>${t("table.interval")}</th><th>${t("table.status")}</th><th></th></tr></thead>
    <tbody>${rows}</tbody></table></div>`;
}

function updateSensorDallasVisibility() {
  const type = document.getElementById("sensor-type").value;
  document.getElementById("sensor-dallas-block").style.display = type === "dallas" ? "" : "none";
}

function renderSensorPinGrid() {
  const type = document.getElementById("sensor-type").value;
  const editId = document.getElementById("sensor-edit-id").value;
  const taken = new Set();
  for (const s of state.sensorsConfig) {
    if (s.id === editId) continue;
    if (type === "dallas" && s.type === "dallas") continue; // Multidrop erlaubt
    taken.add(s.gpio);
  }
  for (const io of state.ioConfig) taken.add(io.gpio);
  renderPinGrid(document.getElementById("sensor-pin-grid"), {
    selected: sensorDialogState.selectedGpio,
    taken,
    onSelect: (gpio) => { sensorDialogState.selectedGpio = gpio; renderSensorPinGrid(); },
  });
}

function openSensorDialog(existing) {
  document.getElementById("form-sensor").reset();
  document.getElementById("sensor-edit-id").value = existing ? existing.id : "";
  document.getElementById("sensor-dialog-title").textContent = existing ? t("sensor.dialog.edit.title") : t("sensor.dialog.add.title");
  document.getElementById("sensor-type").value = existing ? existing.type : "dallas";
  document.getElementById("sensor-label").value = existing ? (existing.label || "") : "";
  document.getElementById("sensor-interval").value = existing ? existing.poll_interval_s : 10;
  document.getElementById("sensor-rom-id").innerHTML = `<option value="">${t("option.first.device")}</option>`
    + (existing && existing.rom_id ? `<option value="${existing.rom_id}" selected>${existing.rom_id}</option>` : "");
  document.getElementById("scan-status").textContent = "";
  sensorDialogState.selectedGpio = existing ? existing.gpio : null;
  updateSensorDallasVisibility();
  renderSensorPinGrid();
  document.getElementById("dialog-sensor").showModal();
}

// ============================================================
// IOs
// ============================================================

async function loadIo() {
  const [config, live] = await Promise.all([api.get("/api/io/config"), api.get("/api/io")]);
  state.ioConfig = config;
  state.ioLive = live;
}

function renderIoTable() {
  const el = document.getElementById("io-table");
  if (!state.ioConfig.length) {
    el.innerHTML = `<div class="empty-state"><strong>${t("io.empty.title")}</strong>${t("io.empty.hint")}</div>`;
    return;
  }
  const rows = state.ioConfig.map((io) => {
    const live = state.ioLive.find((l) => l.id === io.id) || {};
    let stateCell;
    if (io.type === "output" && !io.rule) {
      stateCell = `<label class="switch"><input type="checkbox" ${live.state ? "checked" : ""} data-toggle-io-inline="${io.id}"><span class="switch__track"></span></label>`;
    } else {
      stateCell = `<span class="badge ${live.state ? "ok" : ""}"><span class="dot"></span>${live.state ? t("io.state.on") : t("io.state.off")}</span>`;
    }
    return `<tr>
      <td>${escapeHtml(io.label || io.id)}</td>
      <td>${io.type === "output" ? t("io.output") : t("io.input")}</td>
      <td class="mono">GPIO${io.gpio}</td>
      <td>${io.rule ? ruleSummary(io.rule) : "–"}</td>
      <td>${stateCell}</td>
      <td class="table-actions">
        <button class="btn btn--sm" data-edit-io="${io.id}">${t("btn.edit")}</button>
        <button class="btn btn--sm btn--danger" data-delete-io="${io.id}">${t("btn.delete")}</button>
      </td>
    </tr>`;
  }).join("");
  el.innerHTML = `<div class="table-wrap"><table>
    <thead><tr><th>${t("table.label")}</th><th>${t("table.type")}</th><th>${t("table.gpio")}</th><th>${t("table.rule")}</th><th>${t("table.state")}</th><th></th></tr></thead>
    <tbody>${rows}</tbody></table></div>`;
  el.querySelectorAll("[data-toggle-io-inline]").forEach((cb) => {
    cb.addEventListener("change", () => toggleIo(cb.dataset.toggleIoInline, cb.checked));
  });
}

function updateIoTypeVisibility() {
  const type = document.getElementById("io-type").value;
  document.getElementById("io-output-block").style.display = type === "output" ? "" : "none";
}

function updateIoRuleVisibility() {
  const on = document.getElementById("io-rule-enabled").checked;
  document.getElementById("io-rule-block").style.display = on ? "" : "none";
}

function renderIoPinGrid() {
  const editId = document.getElementById("io-edit-id").value;
  const taken = new Set();
  for (const s of state.sensorsConfig) taken.add(s.gpio);
  for (const io of state.ioConfig) {
    if (io.id !== editId) taken.add(io.gpio);
  }
  renderPinGrid(document.getElementById("io-pin-grid"), {
    selected: ioDialogState.selectedGpio,
    taken,
    onSelect: (gpio) => { ioDialogState.selectedGpio = gpio; renderIoPinGrid(); },
  });
}

function openIoDialog(existing) {
  document.getElementById("form-io").reset();
  document.getElementById("io-edit-id").value = existing ? existing.id : "";
  document.getElementById("io-dialog-title").textContent = existing ? t("io.dialog.edit.title") : t("io.dialog.add.title");
  document.getElementById("io-type").value = existing ? existing.type : "output";
  document.getElementById("io-label").value = existing ? (existing.label || "") : "";
  document.getElementById("io-invert").checked = existing ? !!existing.invert : false;
  document.getElementById("io-initial-state").checked = existing ? !!existing.initial_state : false;

  const rule = existing && existing.rule;
  document.getElementById("io-rule-enabled").checked = !!rule;
  const sensorOptions = state.sensorsConfig.map((s) => `<option value="${s.id}">${escapeHtml(s.label || s.id)}</option>`).join("");
  document.getElementById("io-rule-sensor").innerHTML = sensorOptions || `<option value="">${t("option.no.sensors")}</option>`;
  if (rule) {
    document.getElementById("io-rule-sensor").value = rule.sensor_id;
    document.getElementById("io-rule-field").value = rule.field;
    document.getElementById("io-rule-op").value = rule.operator;
    document.getElementById("io-rule-threshold").value = rule.threshold;
    document.getElementById("io-rule-hysteresis").value = rule.hysteresis || 0;
  } else {
    document.getElementById("io-rule-threshold").value = 25;
    document.getElementById("io-rule-hysteresis").value = 0;
  }

  ioDialogState.selectedGpio = existing ? existing.gpio : null;
  updateIoTypeVisibility();
  updateIoRuleVisibility();
  renderIoPinGrid();
  document.getElementById("dialog-io").showModal();
}

// ============================================================
// Netzwerk / MQTT / SNMP / Auth
// ============================================================

async function loadNetworkForm() {
  const cfg = await api.get("/api/network/config");
  document.getElementById("net-ssid").value = cfg.wifi_ssid || "";
  document.getElementById("net-password").value = "";
  document.getElementById("net-hostname").value = cfg.hostname || "";
}

function populateTimezoneSelect() {
  const select = document.getElementById("time-timezone");
  select.innerHTML = TIMEZONES.map((z) => `<option value="${z.value}">${escapeHtml(z.label)}</option>`).join("")
    + `<option value="__custom__">${t("option.timezone.custom")}</option>`;
}

function updateTimezoneCustomVisibility() {
  const isCustom = document.getElementById("time-timezone").value === "__custom__";
  document.getElementById("time-timezone-custom-wrap").style.display = isCustom ? "" : "none";
}

async function loadTimeForm() {
  populateTimezoneSelect(); // erneut befuellen, falls sich die Sprache seit dem letzten Aufruf geaendert hat
  const cfg = await api.get("/api/time/config");
  document.getElementById("time-enabled").checked = cfg.enabled;
  document.getElementById("time-ntp-server").value = cfg.ntp_server || "";

  const known = TIMEZONES.some((z) => z.value === cfg.timezone);
  document.getElementById("time-timezone").value = known ? cfg.timezone : "__custom__";
  document.getElementById("time-timezone-custom").value = known ? "" : (cfg.timezone || "");
  updateTimezoneCustomVisibility();

  const badge = document.getElementById("time-badge");
  if (!cfg.enabled) {
    badge.className = "badge"; badge.innerHTML = `<span class="dot"></span>${t("status.disabled")}`;
  } else if (cfg.synced) {
    badge.className = "badge ok"; badge.innerHTML = `<span class="dot"></span>${t("status.time.synced")}`;
  } else {
    badge.className = "badge warn"; badge.innerHTML = `<span class="dot"></span>${t("status.time.unsynced")}`;
  }
}

async function loadMqttForm() {
  const [cfg, info] = await Promise.all([api.get("/api/mqtt/config"), api.get("/api/system/info")]);
  document.getElementById("mqtt-enabled").checked = cfg.enabled;
  document.getElementById("mqtt-uri").value = cfg.broker_uri || "";
  document.getElementById("mqtt-user").value = cfg.username || "";
  document.getElementById("mqtt-pass").value = "";
  document.getElementById("mqtt-topic").value = cfg.base_topic || "";
  document.getElementById("mqtt-interval").value = cfg.publish_interval_s || 30;
  document.getElementById("mqtt-client-id").value = cfg.client_id || "";
  document.getElementById("mqtt-cacert").value = cfg.ca_cert || "";

  const badge = document.getElementById("mqtt-badge");
  if (!cfg.enabled) {
    badge.className = "badge"; badge.innerHTML = `<span class="dot"></span>${t("status.disabled")}`;
  } else if (info.mqtt_connected) {
    badge.className = "badge ok"; badge.innerHTML = `<span class="dot"></span>${t("status.connected")}`;
  } else {
    badge.className = "badge warn"; badge.innerHTML = `<span class="dot"></span>${t("status.disconnected")}`;
  }
}

async function loadSnmpForm() {
  const cfg = await api.get("/api/snmp/config");
  document.getElementById("snmp-enabled").checked = cfg.enabled;
  document.getElementById("snmp-community").value = cfg.community || "";
  document.getElementById("snmp-sysname").value = cfg.sys_name || "";
  document.getElementById("snmp-syscontact").value = cfg.sys_contact || "";
  document.getElementById("snmp-syslocation").value = cfg.sys_location || "";

  const badge = document.getElementById("snmp-badge");
  if (!cfg.enabled) {
    badge.className = "badge"; badge.innerHTML = `<span class="dot"></span>${t("status.disabled")}`;
  } else if (cfg.listening) {
    badge.className = "badge ok"; badge.innerHTML = `<span class="dot"></span>${t("status.active")}`;
  } else {
    badge.className = "badge warn"; badge.innerHTML = `<span class="dot"></span>${t("status.restart.needed")}`;
  }
}

async function loadSystemInfo() {
  const info = await api.get("/api/system/info");
  const el = document.getElementById("system-info");
  el.innerHTML = `
    <div class="row row--between"><span class="muted">${t("info.chip")}</span><span>${info.chip} (${t("info.chip.cores", { cores: info.cores })})</span></div>
    <div class="row row--between"><span class="muted">${t("info.idf")}</span><span>${info.idf_version}</span></div>
    <div class="row row--between"><span class="muted">${t("info.uptime")}</span><span>${formatUptime(info.uptime_s)}</span></div>
    <div class="row row--between"><span class="muted">${t("info.free.heap")}</span><span>${(info.free_heap / 1024).toFixed(0)} KB</span></div>
    <div class="row row--between"><span class="muted">${t("info.network")}</span><span class="badge ${info.has_ip ? "ok" : "warn"}"><span class="dot"></span>${info.has_ip ? t("status.connected") : t("footer.noip")}</span></div>
    <div class="row row--between"><span class="muted">${t("info.mqtt")}</span><span class="badge ${info.mqtt_connected ? "ok" : ""}"><span class="dot"></span>${info.mqtt_connected ? t("status.connected") : t("status.disconnected")}</span></div>
    <div class="row row--between"><span class="muted">${t("info.header.pins")}</span><span>${t("info.header.pins.value", { count: info.header_gpio_count })}</span></div>
  `;
}

async function loadAuthForm() {
  const cfg = await api.get("/api/auth/config");
  document.getElementById("auth-enabled").checked = cfg.enabled;
  document.getElementById("auth-username").value = cfg.username || "";
  document.getElementById("auth-password").value = "";
  document.getElementById("auth-password-hint").textContent = cfg.password_set
    ? t("hint.password.set")
    : t("hint.password.notset");
}

// ============================================================
// Hilfe (statische, aus docs/*.md vorgenerierte HTML-Fragmente je Sprache)
// ============================================================

function setActiveHelpNavButton(topic) {
  document.querySelectorAll("#help-nav .help-nav__item").forEach((btn) => {
    btn.classList.toggle("active", btn.dataset.helpTopic === topic);
  });
}

async function loadHelpTopic(topic) {
  if (!HELP_TOPICS.includes(topic)) topic = HELP_TOPICS[0];
  state.helpTopic = topic;
  setActiveHelpNavButton(topic);
  const el = document.getElementById("help-content");
  el.innerHTML = `<p class="skeleton">${t("common.loading")}</p>`;
  try {
    const res = await fetch(`/help/${topic}.${currentLang}.html`);
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    el.innerHTML = await res.text();
  } catch (err) {
    el.innerHTML = "";
    toast(t("toast.help.load.error", { msg: err.message }), "error");
  }
}

// ============================================================
// OTA-Upload (Firmware und Web-UI teilen sich dieselbe Upload-Logik)
// ============================================================

function wireOtaForm(opts) {
  document.getElementById(opts.formId).addEventListener("submit", async (e) => {
    e.preventDefault();
    const file = document.getElementById(opts.fileId).files[0];
    if (!file) {
      toast(t("toast.select.bin.file"), "error");
      return;
    }
    if (!(await confirmDialog(t(opts.confirmTitleKey), t("confirm.ota.msg", { name: file.name, size: (file.size / 1024).toFixed(0) })))) return;

    const wrap = document.getElementById(opts.wrapId);
    const fill = document.getElementById(opts.fillId);
    const btn = document.getElementById(opts.btnId);
    btn.disabled = true;
    wrap.style.display = "";
    fill.style.width = "0%";

    try {
      await new Promise((resolve, reject) => {
        const xhr = new XMLHttpRequest();
        xhr.open("POST", opts.url);
        xhr.upload.addEventListener("progress", (ev) => {
          if (ev.lengthComputable) {
            fill.style.width = Math.round((ev.loaded / ev.total) * 100) + "%";
          }
        });
        xhr.onload = () => {
          (xhr.status >= 200 && xhr.status < 300) ? resolve() : reject(new Error(`HTTP ${xhr.status}: ${xhr.responseText}`));
        };
        xhr.onerror = () => reject(new Error(t("error.network")));
        xhr.send(file);
      });
      toast(t(opts.successMsgKey), "success");
    } catch (err) {
      toast(t("toast.ota.update.failed", { msg: err.message }), "error");
    } finally {
      btn.disabled = false;
    }
  });
}

// ============================================================
// Event-Wiring
// ============================================================

function wireEvents() {
  document.getElementById("nav").addEventListener("click", (e) => {
    const btn = e.target.closest(".nav__item");
    if (btn) setRoute(btn.dataset.route);
  });
  window.addEventListener("hashchange", () => setRoute(location.hash.slice(1)));

  document.getElementById("menu-btn").addEventListener("click", () => {
    document.getElementById("sidebar").classList.toggle("open");
  });

  document.getElementById("theme-toggle").addEventListener("click", () => {
    const current = document.documentElement.getAttribute("data-theme")
      || (window.matchMedia("(prefers-color-scheme: light)").matches ? "light" : "dark");
    applyTheme(current === "dark" ? "light" : "dark");
  });

  document.getElementById("lang-toggle").addEventListener("click", () => {
    setLang(currentLang === "de" ? "en" : "de");
  });

  // Hilfe
  document.getElementById("help-nav").addEventListener("click", (e) => {
    const btn = e.target.closest("[data-help-topic]");
    if (btn) loadHelpTopic(btn.dataset.helpTopic);
  });

  // Sensoren
  document.getElementById("btn-add-sensor").addEventListener("click", () => openSensorDialog(null));
  document.getElementById("sensor-type").addEventListener("change", () => {
    updateSensorDallasVisibility();
    renderSensorPinGrid();
  });
  document.getElementById("btn-scan-bus").addEventListener("click", async () => {
    const statusEl = document.getElementById("scan-status");
    if (sensorDialogState.selectedGpio == null) {
      statusEl.textContent = t("scan.select.gpio.first");
      return;
    }
    statusEl.textContent = t("scan.scanning");
    try {
      const ids = await api.get(`/api/sensors/dallas-scan?gpio=${sensorDialogState.selectedGpio}`);
      const select = document.getElementById("sensor-rom-id");
      select.innerHTML = `<option value="">${t("option.first.device")}</option>`
        + ids.map((id) => `<option value="${id}">${id}</option>`).join("");
      statusEl.textContent = ids.length ? t("scan.found", { n: ids.length }) : t("scan.none.found");
    } catch (err) {
      statusEl.textContent = t("scan.error", { msg: err.message });
    }
  });
  document.getElementById("form-sensor").addEventListener("submit", async (e) => {
    e.preventDefault();
    if (sensorDialogState.selectedGpio == null) {
      toast(t("toast.select.gpio"), "error");
      return;
    }
    const type = document.getElementById("sensor-type").value;
    const editId = document.getElementById("sensor-edit-id").value;
    const list = [...state.sensorsConfig];
    const entry = {
      id: editId || makeId(document.getElementById("sensor-label").value, list.map((s) => s.id)),
      type,
      gpio: sensorDialogState.selectedGpio,
      label: document.getElementById("sensor-label").value.trim(),
      poll_interval_s: parseInt(document.getElementById("sensor-interval").value, 10) || 10,
    };
    if (type === "dallas") {
      const romId = document.getElementById("sensor-rom-id").value;
      if (romId) entry.rom_id = romId;
    }
    const idx = list.findIndex((s) => s.id === entry.id);
    if (idx >= 0) list[idx] = entry; else list.push(entry);
    try {
      await api.put("/api/sensors/config", list);
      toast(t("toast.sensor.saved"), "success");
      document.getElementById("dialog-sensor").close();
      await loadSensors();
      renderSensorsTable();
    } catch (err) {
      toast(t("toast.error", { msg: err.message }), "error");
    }
  });

  // IOs
  document.getElementById("btn-add-io").addEventListener("click", () => openIoDialog(null));
  document.getElementById("io-type").addEventListener("change", updateIoTypeVisibility);
  document.getElementById("io-rule-enabled").addEventListener("change", updateIoRuleVisibility);
  document.getElementById("form-io").addEventListener("submit", async (e) => {
    e.preventDefault();
    if (ioDialogState.selectedGpio == null) {
      toast(t("toast.select.gpio"), "error");
      return;
    }
    const type = document.getElementById("io-type").value;
    const editId = document.getElementById("io-edit-id").value;
    const list = [...state.ioConfig];
    const entry = {
      id: editId || makeId(document.getElementById("io-label").value, list.map((i) => i.id)),
      type,
      gpio: ioDialogState.selectedGpio,
      label: document.getElementById("io-label").value.trim(),
      invert: document.getElementById("io-invert").checked,
    };
    if (type === "output") {
      entry.initial_state = document.getElementById("io-initial-state").checked;
      if (document.getElementById("io-rule-enabled").checked) {
        entry.rule = {
          sensor_id: document.getElementById("io-rule-sensor").value,
          field: document.getElementById("io-rule-field").value,
          operator: document.getElementById("io-rule-op").value,
          threshold: parseFloat(document.getElementById("io-rule-threshold").value) || 0,
          hysteresis: Math.max(0, parseFloat(document.getElementById("io-rule-hysteresis").value) || 0),
        };
      }
    }
    const idx = list.findIndex((i) => i.id === entry.id);
    if (idx >= 0) list[idx] = entry; else list.push(entry);
    try {
      await api.put("/api/io/config", list);
      toast(t("toast.io.saved"), "success");
      document.getElementById("dialog-io").close();
      await loadIo();
      renderIoTable();
    } catch (err) {
      toast(t("toast.error", { msg: err.message }), "error");
    }
  });

  // Netzwerk
  document.getElementById("form-network").addEventListener("submit", async (e) => {
    e.preventDefault();
    const body = {
      wifi_ssid: document.getElementById("net-ssid").value.trim(),
      wifi_password: document.getElementById("net-password").value,
      hostname: document.getElementById("net-hostname").value.trim(),
    };
    try {
      await api.put("/api/network/config", body);
      toast(t("toast.network.saved"), "success");
    } catch (err) {
      toast(t("toast.error", { msg: err.message }), "error");
    }
  });

  // Uhrzeit (NTP)
  document.getElementById("time-timezone").addEventListener("change", updateTimezoneCustomVisibility);
  document.getElementById("form-time").addEventListener("submit", async (e) => {
    e.preventDefault();
    const selected = document.getElementById("time-timezone").value;
    const timezone = selected === "__custom__"
      ? document.getElementById("time-timezone-custom").value.trim()
      : selected;
    const body = {
      enabled: document.getElementById("time-enabled").checked,
      ntp_server: document.getElementById("time-ntp-server").value.trim(),
      timezone,
    };
    try {
      await api.put("/api/time/config", body);
      toast(t("toast.time.saved"), "success");
      await loadTimeForm();
    } catch (err) {
      toast(t("toast.error", { msg: err.message }), "error");
    }
  });

  // MQTT
  document.getElementById("form-mqtt").addEventListener("submit", async (e) => {
    e.preventDefault();
    const body = {
      enabled: document.getElementById("mqtt-enabled").checked,
      broker_uri: document.getElementById("mqtt-uri").value.trim(),
      username: document.getElementById("mqtt-user").value.trim(),
      password: document.getElementById("mqtt-pass").value,
      base_topic: document.getElementById("mqtt-topic").value.trim(),
      publish_interval_s: parseInt(document.getElementById("mqtt-interval").value, 10) || 30,
      client_id: document.getElementById("mqtt-client-id").value.trim(),
      ca_cert: document.getElementById("mqtt-cacert").value,
    };
    try {
      await api.put("/api/mqtt/config", body);
      toast(t("toast.mqtt.saved"), "success");
      await loadMqttForm();
    } catch (err) {
      toast(t("toast.error", { msg: err.message }), "error");
    }
  });

  // SNMP
  document.getElementById("form-snmp").addEventListener("submit", async (e) => {
    e.preventDefault();
    const body = {
      enabled: document.getElementById("snmp-enabled").checked,
      community: document.getElementById("snmp-community").value.trim(),
      sys_name: document.getElementById("snmp-sysname").value.trim(),
      sys_contact: document.getElementById("snmp-syscontact").value.trim(),
      sys_location: document.getElementById("snmp-syslocation").value.trim(),
    };
    try {
      await api.put("/api/snmp/config", body);
      toast(t("toast.snmp.saved"), "success");
      await loadSnmpForm();
    } catch (err) {
      toast(t("toast.error", { msg: err.message }), "error");
    }
  });

  // Auth
  document.getElementById("form-auth").addEventListener("submit", async (e) => {
    e.preventDefault();
    const body = {
      enabled: document.getElementById("auth-enabled").checked,
      username: document.getElementById("auth-username").value.trim(),
      password: document.getElementById("auth-password").value,
    };
    try {
      await api.put("/api/auth/config", body);
      toast(t("toast.auth.saved"), "success");
      await loadAuthForm();
    } catch (err) {
      toast(t("toast.error", { msg: err.message }), "error");
    }
  });

  // Wartung
  document.getElementById("btn-reboot").addEventListener("click", async () => {
    if (!(await confirmDialog(t("confirm.reboot.title"), t("confirm.reboot.msg")))) return;
    try {
      await api.post("/api/system/reboot");
    } catch (e) { /* Verbindung bricht durch den Neustart oft vor der Antwort ab */ }
    toast(t("toast.reboot.running"), "success");
  });
  document.getElementById("btn-factory-reset").addEventListener("click", async () => {
    if (!(await confirmDialog(t("confirm.factory.title"), t("confirm.factory.msg")))) return;
    try {
      await api.post("/api/system/factory-reset");
    } catch (e) { /* siehe oben */ }
    toast(t("toast.factory.running"), "success");
  });

  wireOtaForm({
    formId: "form-ota", fileId: "ota-file", wrapId: "ota-progress-wrap", fillId: "ota-progress-fill",
    btnId: "btn-ota-upload", url: "/api/system/ota", confirmTitleKey: "confirm.ota.firmware.title",
    successMsgKey: "toast.ota.firmware.success",
  });
  wireOtaForm({
    formId: "form-ota-web", fileId: "ota-web-file", wrapId: "ota-web-progress-wrap", fillId: "ota-web-progress-fill",
    btnId: "btn-ota-web-upload", url: "/api/system/ota-web", confirmTitleKey: "confirm.ota.web.title",
    successMsgKey: "toast.ota.web.success",
  });

  // Globale Delegation: Bearbeiten/Löschen-Buttons in Tabellen, Dialog-Schliessen
  document.addEventListener("click", async (e) => {
    const helpLink = e.target.closest("[data-help-link]");
    if (helpLink) {
      e.preventDefault();
      await loadHelpTopic(helpLink.dataset.helpLink);
      const anchor = helpLink.dataset.helpAnchor;
      if (anchor) {
        const target = document.getElementById(anchor);
        if (target) target.scrollIntoView({ behavior: "smooth" });
      }
      return;
    }
    const editSensor = e.target.closest("[data-edit-sensor]");
    if (editSensor) {
      openSensorDialog(state.sensorsConfig.find((s) => s.id === editSensor.dataset.editSensor));
      return;
    }
    const delSensor = e.target.closest("[data-delete-sensor]");
    if (delSensor) {
      const id = delSensor.dataset.deleteSensor;
      if (await confirmDialog(t("confirm.delete.sensor.title"), t("confirm.delete.sensor.msg", { id }))) {
        try {
          const list = state.sensorsConfig.filter((s) => s.id !== id);
          await api.put("/api/sensors/config", list);
          toast(t("toast.sensor.deleted"), "success");
          await loadSensors();
          renderSensorsTable();
        } catch (err) {
          toast(t("toast.error", { msg: err.message }), "error");
        }
      }
      return;
    }
    const editIo = e.target.closest("[data-edit-io]");
    if (editIo) {
      openIoDialog(state.ioConfig.find((i) => i.id === editIo.dataset.editIo));
      return;
    }
    const delIo = e.target.closest("[data-delete-io]");
    if (delIo) {
      const id = delIo.dataset.deleteIo;
      if (await confirmDialog(t("confirm.delete.io.title"), t("confirm.delete.io.msg", { id }))) {
        try {
          const list = state.ioConfig.filter((i) => i.id !== id);
          await api.put("/api/io/config", list);
          toast(t("toast.io.deleted"), "success");
          await loadIo();
          renderIoTable();
        } catch (err) {
          toast(t("toast.error", { msg: err.message }), "error");
        }
      }
      return;
    }
    const closeBtn = e.target.closest("[data-close-dialog]");
    if (closeBtn) {
      document.getElementById(closeBtn.dataset.closeDialog).close();
    }
  });
}

// ============================================================
// Footer-/Statusleiste
// ============================================================

function setStatusDot(id, cls) {
  document.getElementById(id).className = "statusbar__dot" + (cls ? " " + cls : "");
}

function renderStatusBar(info) {
  // Netzwerk
  setStatusDot("sb-network-dot", info.has_ip ? "ok" : "danger");
  const ifaces = [];
  if (info.eth_connected) ifaces.push(t("statusbar.iface.eth"));
  if (info.wifi_connected) ifaces.push(t("statusbar.iface.wifi"));
  document.getElementById("sb-network-value").textContent = info.has_ip
    ? (ifaces.length ? ifaces.join(" + ") : t("status.connected"))
    : t("footer.noip");

  // MQTT
  if (!info.mqtt_enabled) {
    setStatusDot("sb-mqtt-dot", null);
    document.getElementById("sb-mqtt-value").textContent = t("status.disabled");
  } else {
    setStatusDot("sb-mqtt-dot", info.mqtt_connected ? "ok" : "warn");
    document.getElementById("sb-mqtt-value").textContent = info.mqtt_connected ? t("status.connected") : t("status.disconnected");
  }

  // SNMP
  if (!info.snmp_enabled) {
    setStatusDot("sb-snmp-dot", null);
    document.getElementById("sb-snmp-value").textContent = t("status.disabled");
  } else {
    setStatusDot("sb-snmp-dot", info.snmp_listening ? "ok" : "warn");
    document.getElementById("sb-snmp-value").textContent = info.snmp_listening ? t("status.active") : t("status.restart.needed");
  }

  // Uhrzeit-Anker fuer die sekuendlich tickende Client-Uhr (siehe tickClock())
  state.timeAnchor = {
    unixMs: info.unix_time_s * 1000,
    offsetS: info.utc_offset_s,
    capturedAtPerfMs: performance.now(),
    synced: info.time_synced,
  };
}

function tickClock() {
  const el = document.getElementById("sb-clock");
  if (!state.timeAnchor) {
    el.textContent = "--:--:--";
    return;
  }
  const elapsedMs = performance.now() - state.timeAnchor.capturedAtPerfMs;
  const nowLocalMs = state.timeAnchor.unixMs + state.timeAnchor.offsetS * 1000 + elapsedMs;
  const d = new Date(nowLocalMs);
  const hh = String(d.getUTCHours()).padStart(2, "0");
  const mm = String(d.getUTCMinutes()).padStart(2, "0");
  const ss = String(d.getUTCSeconds()).padStart(2, "0");
  const dd = String(d.getUTCDate()).padStart(2, "0");
  const MM = String(d.getUTCMonth() + 1).padStart(2, "0");
  el.textContent = `${hh}:${mm}:${ss}`;
  el.classList.toggle("unsynced", !state.timeAnchor.synced);
  el.title = `${dd}.${MM}.${d.getUTCFullYear()} ${hh}:${mm}:${ss} – `
    + (state.timeAnchor.synced ? t("statusbar.clock.synced") : t("statusbar.clock.unsynced"));
}

async function pollFooterStatus() {
  try {
    const info = await api.get("/api/system/info");
    state.uptimeMs = info.uptime_s * 1000;
    document.getElementById("brand-dot").classList.toggle("ok", info.has_ip);
    document.getElementById("footer-ip").textContent = info.has_ip ? t("footer.connected") : t("footer.noip");
    document.getElementById("topbar-meta").textContent = t("topbar.meta.info", {
      uptime: formatUptime(info.uptime_s),
      heap: (info.free_heap / 1024).toFixed(0),
    });
    renderStatusBar(info);
  } catch (e) { /* still show old status */ }
}

// ============================================================
// Init
// ============================================================

async function init() {
  const savedTheme = localStorage.getItem("tw-theme");
  if (savedTheme) applyTheme(savedTheme);

  applyStaticTranslations();
  updateLangToggleLabel();
  populateTimezoneSelect();

  wireEvents();

  try {
    state.boardPins = await api.get("/api/board/pins");
  } catch (err) {
    toast(t("toast.boardpins.error", { msg: err.message }), "error");
  }

  setRoute(location.hash.slice(1) || "dashboard");
  pollFooterStatus();
  setInterval(pollFooterStatus, 10000);
  tickClock();
  setInterval(tickClock, 1000);
}

init();
