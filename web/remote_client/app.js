const storageKey = "vibe-remote-client";
const saved = JSON.parse(localStorage.getItem(storageKey) || "{}");

const hostInput = document.getElementById("host");
const portInput = document.getElementById("port");
const tokenInput = document.getElementById("token");
const providerInput = document.getElementById("provider");
const sessionInput = document.getElementById("session");
const titleInput = document.getElementById("title");
const workspaceInput = document.getElementById("workspace");
const commandInput = document.getElementById("command");

const hostOutput = document.getElementById("host-output");
const pairingOutput = document.getElementById("pairing-output");
const sessionsSummaryEl = document.getElementById("sessions-summary");
const sessionsListEl = document.getElementById("sessions-list");
const eventsEl = document.getElementById("events");
const connectionStateEl = document.getElementById("connection-state");
const controllerStateEl = document.getElementById("controller-state");
const sessionStateEl = document.getElementById("session-state");
const terminalCaptionEl = document.getElementById("terminal-caption");
const terminalSizeEl = document.getElementById("terminal-size");
const selectedSessionTitleEl = document.getElementById("selected-session-title");
const selectedSessionSubtitleEl = document.getElementById("selected-session-subtitle");
const selectedSessionBadgesEl = document.getElementById("selected-session-badges");
const selectedSessionMetaEl = document.getElementById("selected-session-meta");
const fileChipListEl = document.getElementById("file-chip-list");
const fileMetaEl = document.getElementById("file-meta");
const fileOutputEl = document.getElementById("file-output");
const snapshotSignalsEl = document.getElementById("snapshot-signals");

const saveTokenBtn = document.getElementById("save-token");
const clearTokenBtn = document.getElementById("clear-token");
const refreshHostBtn = document.getElementById("refresh-host");
const startPairingBtn = document.getElementById("start-pairing");
const listSessionsBtn = document.getElementById("list-sessions");
const createSessionBtn = document.getElementById("create-session");
const connectBtn = document.getElementById("connect");
const disconnectBtn = document.getElementById("disconnect");
const requestControlBtn = document.getElementById("request-control");
const releaseControlBtn = document.getElementById("release-control");
const sendResizeBtn = document.getElementById("send-resize");
const stopBtn = document.getElementById("stop");
const { Terminal } = window;
const { FitAddon } = window.FitAddon;

const defaultHost = window.location.hostname || "127.0.0.1";
const defaultPort = window.location.port || (window.location.protocol === "https:" ? "443" : "80");
hostInput.value = saved.host || defaultHost;
portInput.value = saved.port || defaultPort;
tokenInput.value = saved.token || "";
sessionInput.value = saved.sessionId || "s_1";
titleInput.value = saved.title || "smoke-session";
workspaceInput.value = saved.workspaceRoot || ".";
providerInput.value = saved.provider || "codex";
commandInput.value = saved.command || "";

const state = {
  sessions: [],
  selectedSessionId: saved.selectedSessionId || sessionInput.value.trim() || "",
  selectedSession: null,
  selectedSnapshot: null,
  selectedFilePath: "",
  selectedFileContent: "Select a recent file to read its current content.",
  selectedFileSizeBytes: 0,
  selectedFileTruncated: false
};

const terminal = new Terminal({
  convertEol: false,
  cursorBlink: true,
  fontFamily: "Iosevka Comfy, SF Mono, Menlo, monospace",
  fontSize: 14,
  scrollback: 5000,
  theme: {
    background: "#171613",
    foreground: "#ece2d0",
    cursor: "#b05734",
    selectionBackground: "rgba(176, 87, 52, 0.28)"
  }
});
const fitAddon = new FitAddon();
terminal.loadAddon(fitAddon);
terminal.open(document.getElementById("terminal"));
fitAddon.fit();

let ws = null;
let overviewWs = null;
let resizeTimer = null;
let connectTimeout = null;
let overviewConnectTimeout = null;
let pairingPollTimer = null;
let connectionState = "disconnected";
let sessionState = "unknown";
let controllerState = "observer";
let inputEnabled = false;

function saveSettings() {
  localStorage.setItem(storageKey, JSON.stringify({
    host: hostInput.value.trim(),
    port: portInput.value.trim(),
    token: tokenInput.value.trim(),
    sessionId: sessionInput.value.trim(),
    title: titleInput.value.trim(),
    workspaceRoot: workspaceInput.value.trim(),
    provider: providerInput.value,
    command: commandInput.value,
    selectedSessionId: state.selectedSessionId
  }));
}

function appendEvent(text) {
  const timestamp = new Date().toLocaleTimeString();
  eventsEl.textContent += `[${timestamp}] ${text}\n`;
  eventsEl.scrollTop = eventsEl.scrollHeight;
}

function setStates(nextConnection, nextController, nextSession) {
  if (typeof nextConnection === "string") {
    connectionState = nextConnection;
  }
  if (typeof nextController === "string") {
    controllerState = nextController;
  }
  if (typeof nextSession === "string") {
    sessionState = nextSession;
  }
  connectionStateEl.textContent = connectionState;
  controllerStateEl.textContent = controllerState;
  sessionStateEl.textContent = sessionState;
  terminalCaptionEl.textContent =
    `${sessionInput.value.trim() || "no-session"} @ ${hostInput.value.trim()}:${portInput.value.trim()}`;
}

function updateTerminalSizeDisplay() {
  terminalSizeEl.textContent = `${terminal.cols} x ${terminal.rows}`;
}

function currentProtocol() {
  return window.location.protocol === "https:" ? "https:" : "http:";
}

function currentWebSocketProtocol() {
  return window.location.protocol === "https:" ? "wss:" : "ws:";
}

function baseUrl() {
  return `${currentProtocol()}//${hostInput.value.trim()}:${portInput.value.trim()}`;
}

function websocketUrl() {
  const token = encodeURIComponent(tokenInput.value.trim());
  return `${currentWebSocketProtocol()}//${hostInput.value.trim()}:${portInput.value.trim()}/ws/sessions/${sessionInput.value.trim()}?access_token=${token}`;
}

function overviewWebsocketUrl() {
  const token = encodeURIComponent(tokenInput.value.trim());
  return `${currentWebSocketProtocol()}//${hostInput.value.trim()}:${portInput.value.trim()}/ws/overview?access_token=${token}`;
}

async function fetchJson(path, options = {}) {
  const headers = new Headers(options.headers || {});
  if (tokenInput.value.trim()) {
    headers.set("authorization", `Bearer ${tokenInput.value.trim()}`);
  }
  if (options.body && !headers.has("content-type")) {
    headers.set("content-type", "application/json");
  }

  const response = await fetch(`${baseUrl()}${path}`, {
    ...options,
    headers
  });

  const text = await response.text();
  let parsed = null;
  if (text) {
    try {
      parsed = JSON.parse(text);
    } catch {
      parsed = text;
    }
  }

  if (!response.ok) {
    throw new Error(typeof parsed === "string" ? parsed : JSON.stringify(parsed));
  }

  return parsed;
}

function renderJson(el, value) {
  el.textContent = typeof value === "string" ? value : JSON.stringify(value, null, 2);
}

function decodeBase64ToString(encoded) {
  const binary = atob(encoded || "");
  const bytes = Uint8Array.from(binary, (char) => char.charCodeAt(0));
  return new TextDecoder().decode(bytes);
}

function formatTimestamp(value) {
  if (!value) {
    return "n/a";
  }
  return new Date(value).toLocaleString();
}

function bucketSession(session) {
  if (session.isActive) {
    return "active";
  }
  if (session.isRecovered) {
    return "recovered";
  }
  return "inactive";
}

function badge(text, tone = "") {
  const el = document.createElement("span");
  el.className = `badge ${tone}`.trim();
  el.textContent = text;
  return el;
}

function metaCell(label, value) {
  const cell = document.createElement("div");
  cell.className = "meta-cell";
  const key = document.createElement("span");
  key.textContent = label;
  const val = document.createElement("strong");
  val.textContent = value;
  cell.append(key, val);
  return cell;
}

function describeController(kind, clientId = "") {
  if (kind === "remote") {
    return clientId ? `Remote: ${clientId}` : "Remote controller";
  }
  if (kind === "host") {
    return clientId ? `Host: ${clientId}` : "Host terminal";
  }
  return "Observer only";
}

function detailValue(session, label) {
  switch (label) {
    case "Provider":
      return session.provider || "n/a";
    case "Workspace":
      return session.workspaceRoot || "n/a";
    case "Created":
      return formatTimestamp(session.createdAtUnixMs);
    case "Last Activity":
      return formatTimestamp(session.lastActivityAtUnixMs);
    case "Last Output":
      return formatTimestamp(session.lastOutputAtUnixMs);
    case "Git":
      return session.gitDirty ? `dirty (${session.gitBranch || "unknown"})` : (session.gitBranch || "clean");
    default:
      return "n/a";
  }
}

function updateButtons() {
  const readyState = ws ? ws.readyState : WebSocket.CLOSED;
  const connected = readyState === WebSocket.OPEN;
  const connecting = readyState === WebSocket.CONNECTING;
  const selected = Boolean(state.selectedSessionId);
  const selectedSession = state.selectedSession;
  const selectedIsLive = Boolean(selectedSession && selectedSession.isActive);
  const selectedCanStop = Boolean(
    selectedSession &&
    !selectedSession.isRecovered &&
    selectedSession.status !== "Exited" &&
    selectedSession.status !== "Error"
  );
  const interactive = sessionState === "Running" || sessionState === "AwaitingInput";
  const remoteHasControl = controllerState === "remote";

  connectBtn.disabled = !selected || !selectedIsLive || connected || connecting;
  disconnectBtn.disabled = !(connected || connecting);
  requestControlBtn.disabled = !connected || !interactive || remoteHasControl;
  releaseControlBtn.disabled = !connected || !remoteHasControl;
  sendResizeBtn.disabled = !connected || !interactive || !remoteHasControl;
  stopBtn.disabled = !connected || !selectedCanStop;
}

function clearConnectTimeout() {
  if (connectTimeout !== null) {
    clearTimeout(connectTimeout);
    connectTimeout = null;
  }
}

function clearOverviewConnectTimeout() {
  if (overviewConnectTimeout !== null) {
    clearTimeout(overviewConnectTimeout);
    overviewConnectTimeout = null;
  }
}

function clearPairingPoll() {
  if (pairingPollTimer !== null) {
    clearTimeout(pairingPollTimer);
    pairingPollTimer = null;
  }
}

function setInputEnabled(enabled) {
  inputEnabled = enabled;
  terminal.options.disableStdin = !enabled;
}

async function pollPairingClaim(pairingId, code) {
  try {
    const payload = await fetchJson("/pairing/claim", {
      method: "POST",
      body: JSON.stringify({ pairingId, code })
    });
    if (payload && payload.token) {
      tokenInput.value = payload.token;
      saveSettings();
      closeOverviewSocket();
      connectOverviewSocket();
      renderJson(pairingOutput, {
        pairingId,
        code,
        status: "approved",
        token: payload.token
      });
      appendEvent(`pairing approved: token received for ${pairingId}`);
      clearPairingPoll();
      return;
    }
  } catch (error) {
    appendEvent(`pairing claim failed: ${String(error)}`);
    clearPairingPoll();
    return;
  }

  pairingPollTimer = setTimeout(() => {
    pollPairingClaim(pairingId, code);
  }, 2000);
}

function sendJson(payload) {
  if (!ws || ws.readyState !== WebSocket.OPEN) {
    appendEvent("websocket not open");
    return;
  }
  ws.send(JSON.stringify(payload));
}

function sendResize() {
  updateTerminalSizeDisplay();
  sendJson({
    type: "terminal.resize",
    cols: terminal.cols,
    rows: terminal.rows
  });
}

function scheduleResize() {
  if (resizeTimer !== null) {
    clearTimeout(resizeTimer);
  }
  resizeTimer = setTimeout(() => {
    fitAddon.fit();
    updateTerminalSizeDisplay();
    if (ws && ws.readyState === WebSocket.OPEN) {
      sendResize();
    }
  }, 80);
}

function applySelectedSession(session) {
  state.selectedSession = session || null;
  state.selectedSessionId = session ? session.sessionId : "";
  state.selectedSnapshot = null;
  if (session) {
    sessionInput.value = session.sessionId;
    titleInput.value = session.title || titleInput.value;
    sessionState = session.status || "unknown";
    controllerState =
      session.controllerKind === "remote" ? "remote" :
      session.controllerKind === "host" ? "host" :
      "observer";
  } else {
    sessionState = "unknown";
    controllerState = "observer";
  }
  setStates(connectionState, controllerState, sessionState);
  saveSettings();
  renderSelectedSession();
  updateButtons();
}

function renderSessions(sessions) {
  state.sessions = Array.isArray(sessions) ? sessions : [];
  sessionsSummaryEl.innerHTML = "";
  sessionsListEl.innerHTML = "";

  if (state.sessions.length === 0) {
    sessionsListEl.innerHTML = '<div class="empty-note">No sessions returned.</div>';
    applySelectedSession(null);
    return;
  }

  const sorted = [...state.sessions].sort((left, right) =>
    String(left.sessionId).localeCompare(String(right.sessionId)));
  const active = sorted.filter((session) => bucketSession(session) === "active");
  const recovered = sorted.filter((session) => bucketSession(session) === "recovered");
  const inactive = sorted.filter((session) => bucketSession(session) === "inactive");

  sessionsSummaryEl.append(
    badge(`${active.length} active`, active.length > 0 ? "good" : "muted"),
    badge(`${recovered.length} recovered`, recovered.length > 0 ? "warn" : "muted"),
    badge(`${inactive.length} inactive`, inactive.length > 0 ? "" : "muted")
  );

  const currentSelected =
    sorted.find((session) => session.sessionId === state.selectedSessionId) ||
    sorted.find((session) => session.sessionId === sessionInput.value.trim()) ||
    sorted[0];
  applySelectedSession(currentSelected || null);

  function renderSection(title, subset) {
    const section = document.createElement("section");
    section.className = "session-section";
    section.appendChild(badge(`${title} (${subset.length})`, "muted"));

    if (subset.length === 0) {
      const empty = document.createElement("div");
      empty.className = "empty-note";
      empty.textContent = "None.";
      section.appendChild(empty);
      return section;
    }

    for (const session of subset) {
      const card = document.createElement("article");
      card.className =
        `session-card state-${bucketSession(session)}${session.sessionId === state.selectedSessionId ? " selected" : ""}`;

      const head = document.createElement("div");
      head.className = "session-head";
      const titleBlock = document.createElement("div");
      const title = document.createElement("div");
      title.className = "session-title";
      title.textContent = session.title || "(untitled)";
      const subtitle = document.createElement("div");
      subtitle.className = "session-subtitle";
      subtitle.textContent = `${session.sessionId} · ${session.provider}`;
      titleBlock.append(title, subtitle);

      const actions = document.createElement("div");
      actions.className = "session-actions";
      const openButton = document.createElement("button");
      openButton.textContent = "Open";
      openButton.addEventListener("click", async () => {
        applySelectedSession(session);
        await loadSelectedSnapshot();
        appendEvent(`selected session ${session.sessionId}`);
      });
      actions.appendChild(openButton);
      head.append(titleBlock, actions);

      const badges = document.createElement("div");
      badges.className = "badge-row";
      badges.append(
        badge(session.activityState || "unknown", session.isActive ? "good" : session.isRecovered ? "warn" : "muted"),
        badge(session.status || "unknown", session.status === "Error" ? "warn" : ""),
        badge(describeController(session.controllerKind, session.controllerClientId),
              session.controllerKind === "remote" ? "warn" : "muted"),
        badge(`${session.attachedClientCount ?? 0} clients`)
      );

      const meta = document.createElement("div");
      meta.className = "meta-grid compact";
      meta.append(
        metaCell("Last Activity", formatTimestamp(session.lastActivityAtUnixMs)),
        metaCell("Files", String(session.recentFileChangeCount ?? 0)),
        metaCell("Git", detailValue(session, "Git")),
        metaCell("Workspace", session.workspaceRoot || "n/a")
      );

      card.append(head, badges, meta);
      section.appendChild(card);
    }

    return section;
  }

  sessionsListEl.append(
    renderSection("Active", active),
    renderSection("Quiet", inactive),
    renderSection("Stopped / Recovered", recovered)
  );
}

function renderSelectedSession() {
  const session = state.selectedSession;
  selectedSessionBadgesEl.innerHTML = "";
  selectedSessionMetaEl.innerHTML = "";

  if (!session) {
    selectedSessionTitleEl.textContent = "No session selected";
    selectedSessionSubtitleEl.textContent = "Pick a session from the inventory to load details.";
    snapshotSignalsEl.innerHTML = "";
    fileChipListEl.innerHTML = '<div class="empty-note">Select a session to inspect recent file changes.</div>';
    fileMetaEl.innerHTML = "";
    fileOutputEl.textContent = "Select a recent file to read its current content.";
    updateButtons();
    return;
  }

  selectedSessionTitleEl.textContent = session.title || "(untitled)";
  selectedSessionSubtitleEl.textContent =
    `${session.sessionId} · ${session.provider} · ${session.workspaceRoot || "no workspace"}`;
  selectedSessionBadgesEl.append(
    badge(session.activityState || "unknown", session.isActive ? "good" : session.isRecovered ? "warn" : "muted"),
    badge(session.status || "unknown", session.status === "Error" ? "warn" : ""),
    badge(session.supervisionState || "unknown"),
    badge(describeController(session.controllerKind, session.controllerClientId),
          session.controllerKind === "remote" ? "warn" : "muted")
  );

  selectedSessionMetaEl.append(
    metaCell("Provider", detailValue(session, "Provider")),
    metaCell("Workspace", detailValue(session, "Workspace")),
    metaCell("Created", detailValue(session, "Created")),
    metaCell("Last Activity", detailValue(session, "Last Activity")),
    metaCell("Last Output", detailValue(session, "Last Output")),
    metaCell("Git", detailValue(session, "Git"))
  );

  renderSnapshotDetails();
  updateButtons();
}

function renderSnapshotDetails() {
  const snapshot = state.selectedSnapshot;
  snapshotSignalsEl.innerHTML = "";

  if (!snapshot) {
    snapshotSignalsEl.append(
      metaCell("Recent Files", "not loaded"),
      metaCell("Sequence", "n/a"),
      metaCell("Tail", "load a session")
    );
    return;
  }

  const recentFiles = Array.isArray(snapshot.recentFileChanges) ? snapshot.recentFileChanges : [];
  snapshotSignalsEl.append(
    metaCell("Recent Files", String(recentFiles.length)),
    metaCell("Sequence", String(snapshot.currentSequence ?? 0)),
    metaCell("Status", state.selectedSession ? state.selectedSession.status || "unknown" : "unknown"),
    metaCell("Controller", state.selectedSession
      ? describeController(state.selectedSession.controllerKind, state.selectedSession.controllerClientId)
      : "unknown")
  );

  fileChipListEl.innerHTML = "";
  if (recentFiles.length === 0) {
    fileChipListEl.innerHTML = '<div class="empty-note">No recent file changes are available yet.</div>';
  } else {
    for (const workspacePath of recentFiles) {
      const button = document.createElement("button");
      button.className = workspacePath === state.selectedFilePath ? "selected" : "";
      button.textContent = workspacePath;
      button.addEventListener("click", () => inspectFile(workspacePath));
      fileChipListEl.appendChild(button);
    }
  }

  renderSelectedFile();
}

function renderSelectedFile() {
  fileMetaEl.innerHTML = "";
  fileOutputEl.textContent = state.selectedFileContent;

  if (!state.selectedFilePath) {
    return;
  }

  fileMetaEl.append(
    metaCell("Path", state.selectedFilePath),
    metaCell("Size", `${state.selectedFileSizeBytes} bytes`),
    metaCell("Status", state.selectedFileTruncated ? "truncated" : "full"),
    metaCell("Session", state.selectedSessionId || "n/a")
  );
}

async function loadSelectedSnapshot() {
  if (!state.selectedSessionId) {
    return;
  }

  try {
    const snapshot = await fetchJson(`/sessions/${encodeURIComponent(state.selectedSessionId)}/snapshot`);
    state.selectedSnapshot = snapshot;
    const files = Array.isArray(snapshot.recentFileChanges) ? snapshot.recentFileChanges : [];
    if (!files.includes(state.selectedFilePath)) {
      state.selectedFilePath = "";
      state.selectedFileContent = "Select a recent file to read its current content.";
      state.selectedFileSizeBytes = 0;
      state.selectedFileTruncated = false;
    }
    renderSelectedSession();
  } catch (error) {
    appendEvent(`snapshot load failed for ${state.selectedSessionId}: ${String(error)}`);
  }
}

async function inspectFile(workspacePath) {
  if (!state.selectedSessionId) {
    return;
  }

  try {
    const payload = await fetchJson(
      `/sessions/${encodeURIComponent(state.selectedSessionId)}/file?path=${encodeURIComponent(workspacePath)}`
    );
    state.selectedFilePath = payload.workspacePath || workspacePath;
    state.selectedFileContent = decodeBase64ToString(payload.contentBase64 || "");
    state.selectedFileSizeBytes = Number(payload.sizeBytes || 0);
    state.selectedFileTruncated = Boolean(payload.truncated);
    renderSnapshotDetails();
    appendEvent(`loaded file ${workspacePath}`);
  } catch (error) {
    appendEvent(`file read failed for ${workspacePath}: ${String(error)}`);
  }
}

async function refreshHost() {
  try {
    const payload = await fetchJson("/host/info");
    renderJson(hostOutput, payload);
    connectOverviewSocket();
  } catch (error) {
    renderJson(hostOutput, `host info failed: ${String(error)}`);
  }
}

async function startPairing() {
  try {
    clearPairingPoll();
    const payload = await fetchJson("/pairing/request", {
      method: "POST",
      body: JSON.stringify({
        deviceName: "remote-browser",
        deviceType: "browser"
      })
    });
    renderJson(pairingOutput, {
      ...payload,
      nextStep: "Approve this pairing in the localhost host UI. This page will poll and store the token automatically."
    });
    appendEvent(`pairing requested: ${payload.pairingId}`);
    pairingPollTimer = setTimeout(() => {
      pollPairingClaim(payload.pairingId, payload.code);
    }, 1500);
  } catch (error) {
    renderJson(pairingOutput, `pairing failed: ${String(error)}`);
  }
}

async function listSessions() {
  try {
    const payload = await fetchJson("/sessions");
    renderSessions(payload);
    await loadSelectedSnapshot();
    connectOverviewSocket();
    appendEvent(`loaded ${Array.isArray(payload) ? payload.length : 0} sessions`);
  } catch (error) {
    sessionsListEl.innerHTML = `<div class="empty-note">session list failed: ${String(error)}</div>`;
    appendEvent(`session list failed: ${String(error)}`);
  }
}

function parseCommandInput(input) {
  if (!input) {
    return null;
  }

  const parts = [];
  let current = "";
  let quote = null;

  for (let index = 0; index < input.length; index += 1) {
    const char = input[index];
    if (quote !== null) {
      if (char === quote) {
        quote = null;
      } else if (char === "\\" && index + 1 < input.length) {
        index += 1;
        current += input[index];
      } else {
        current += char;
      }
      continue;
    }

    if (char === "\"" || char === "'") {
      quote = char;
      continue;
    }

    if (/\s/.test(char)) {
      if (current) {
        parts.push(current);
        current = "";
      }
      continue;
    }

    if (char === "\\" && index + 1 < input.length) {
      index += 1;
      current += input[index];
      continue;
    }

    current += char;
  }

  if (quote !== null) {
    throw new Error("unclosed quote in explicit command");
  }
  if (current) {
    parts.push(current);
  }
  return parts.length > 0 ? parts : null;
}

async function createSession() {
  try {
    const request = {
      provider: providerInput.value,
      workspaceRoot: workspaceInput.value.trim(),
      title: titleInput.value.trim()
    };
    const explicitCommand = parseCommandInput(commandInput.value.trim());
    if (explicitCommand !== null) {
      request.command = explicitCommand;
    }
    const payload = await fetchJson("/sessions", {
      method: "POST",
      body: JSON.stringify(request)
    });
    if (payload && payload.sessionId) {
      sessionInput.value = payload.sessionId;
      state.selectedSessionId = payload.sessionId;
      saveSettings();
      appendEvent(`created session ${payload.sessionId}`);
      await listSessions();
      connectOverviewSocket();
    }
  } catch (error) {
    appendEvent(`create session failed: ${String(error)}`);
  }
}

function closeSocket() {
  clearConnectTimeout();
  if (ws) {
    ws.close();
    ws = null;
  }
  setInputEnabled(false);
  setStates("disconnected", controllerState === "host" ? "host" : "observer", sessionState);
  updateButtons();
}

function closeOverviewSocket() {
  clearOverviewConnectTimeout();
  if (overviewWs) {
    overviewWs.close();
    overviewWs = null;
  }
}

function connectOverviewSocket() {
  if (!tokenInput.value.trim()) {
    return;
  }
  if (overviewWs &&
      (overviewWs.readyState === WebSocket.OPEN ||
       overviewWs.readyState === WebSocket.CONNECTING)) {
    return;
  }

  overviewWs = new WebSocket(overviewWebsocketUrl());
  clearOverviewConnectTimeout();
  overviewConnectTimeout = setTimeout(() => {
    if (overviewWs && overviewWs.readyState !== WebSocket.OPEN) {
      appendEvent(`overview websocket timeout (state=${overviewWs.readyState})`);
      closeOverviewSocket();
    }
  }, 5000);

  overviewWs.addEventListener("open", () => {
    clearOverviewConnectTimeout();
    appendEvent("overview websocket open");
  });

  overviewWs.addEventListener("message", async (event) => {
    try {
      const payload = JSON.parse(event.data);
      if (payload.type === "sessions.snapshot" && Array.isArray(payload.sessions)) {
        renderSessions(payload.sessions);
        if (state.selectedSessionId) {
          await loadSelectedSnapshot();
        }
        return;
      }
      appendEvent(`overview event: ${payload.type || "unknown"}`);
    } catch (error) {
      appendEvent(`overview parse error: ${String(error)}`);
    }
  });

  overviewWs.addEventListener("close", (event) => {
    clearOverviewConnectTimeout();
    appendEvent(`overview websocket closed (code=${event.code}, clean=${event.wasClean})`);
    overviewWs = null;
  });

  overviewWs.addEventListener("error", () => {
    appendEvent("overview websocket error");
  });
}

function connect() {
  saveSettings();
  if (ws && ws.readyState === WebSocket.OPEN) {
    return;
  }

  if (!tokenInput.value.trim()) {
    appendEvent("token is required before connecting");
    return;
  }

  if (!sessionInput.value.trim()) {
    appendEvent("select a session before connecting");
    return;
  }

  terminal.clear();
  appendEvent(`connecting ${websocketUrl()}`);
  ws = new WebSocket(websocketUrl());
  setStates("connecting", controllerState, sessionState);
  updateButtons();
  clearConnectTimeout();
  connectTimeout = setTimeout(() => {
    if (ws && ws.readyState !== WebSocket.OPEN) {
      appendEvent(`websocket connect timeout (state=${ws.readyState})`);
      closeSocket();
    }
  }, 5000);

  ws.addEventListener("open", () => {
    clearConnectTimeout();
    setStates("connected", controllerState, sessionState);
    fitAddon.fit();
    updateTerminalSizeDisplay();
    updateButtons();
    terminal.focus();
    appendEvent("websocket open");
  });

  ws.addEventListener("message", async (event) => {
    try {
      const payload = JSON.parse(event.data);
      if (payload.type === "terminal.output" && payload.dataEncoding === "base64") {
        terminal.write(decodeBase64ToString(payload.dataBase64 || ""));
        return;
      }

      if (payload.type === "session.updated") {
        const nextController =
          payload.controllerKind === "remote" ? "remote" :
          payload.controllerKind === "host" ? "host" : "observer";
        setStates(connectionState, nextController, payload.status || sessionState);
        setInputEnabled(nextController === "remote");
        const updated = state.sessions.find((session) => session.sessionId === payload.sessionId);
        if (updated) {
          updated.status = payload.status || updated.status;
          updated.controllerKind = payload.controllerKind || updated.controllerKind;
          updated.controllerClientId = payload.controllerClientId || updated.controllerClientId;
          if (updated.sessionId === state.selectedSessionId) {
            state.selectedSession = updated;
            renderSelectedSession();
          }
        }
      } else if (payload.type === "session.activity") {
        appendEvent(`activity: ${payload.activityState || "unknown"} | seq=${payload.currentSequence ?? 0} | files=${payload.recentFileChangeCount ?? 0} | git=${payload.gitDirty ? "dirty" : "clean"}:${payload.gitBranch || "-"}`);
        await loadSelectedSnapshot();
      } else if (payload.type === "session.exited") {
        setStates(connectionState, controllerState, payload.status || "Exited");
        setInputEnabled(false);
        const updated = state.sessions.find((session) => session.sessionId === state.selectedSessionId);
        if (updated) {
          updated.status = payload.status || "Exited";
          updated.isActive = false;
          state.selectedSession = updated;
          renderSelectedSession();
        }
      } else if (payload.type === "error") {
        appendEvent(`server error: ${payload.code} | ${payload.message}`);
      } else {
        appendEvent(JSON.stringify(payload));
      }
    } catch (error) {
      appendEvent(`parse error: ${String(error)}`);
    }
  });

  ws.addEventListener("close", (event) => {
    clearConnectTimeout();
    appendEvent(`websocket closed (code=${event.code}, clean=${event.wasClean})`);
    closeSocket();
  });

  ws.addEventListener("error", () => {
    appendEvent(`websocket error (state=${ws ? ws.readyState : "n/a"})`);
    setStates("error", controllerState, sessionState);
    updateButtons();
  });
}

terminal.onData((data) => {
  if (!inputEnabled) {
    return;
  }
  sendJson({ type: "terminal.input", data });
});

terminal.attachCustomKeyEventHandler(() => true);

const resizeObserver = new ResizeObserver(() => scheduleResize());
resizeObserver.observe(document.getElementById("terminal"));
window.addEventListener("resize", scheduleResize);

saveTokenBtn.addEventListener("click", () => {
  saveSettings();
  closeOverviewSocket();
  connectOverviewSocket();
  appendEvent("token saved locally");
});
clearTokenBtn.addEventListener("click", () => {
  tokenInput.value = "";
  saveSettings();
  closeOverviewSocket();
  appendEvent("token cleared");
});
refreshHostBtn.addEventListener("click", refreshHost);
startPairingBtn.addEventListener("click", startPairing);
listSessionsBtn.addEventListener("click", listSessions);
createSessionBtn.addEventListener("click", createSession);
connectBtn.addEventListener("click", connect);
disconnectBtn.addEventListener("click", closeSocket);
requestControlBtn.addEventListener("click", () => sendJson({ type: "session.control.request", kind: "remote" }));
releaseControlBtn.addEventListener("click", () => {
  setInputEnabled(false);
  sendJson({ type: "session.control.release" });
});
sendResizeBtn.addEventListener("click", () => {
  fitAddon.fit();
  sendResize();
});
stopBtn.addEventListener("click", () => sendJson({ type: "session.stop" }));

for (const input of [hostInput, portInput, tokenInput, providerInput, sessionInput, titleInput, workspaceInput, commandInput]) {
  input.addEventListener("change", saveSettings);
}

sessionInput.addEventListener("change", async () => {
  state.selectedSessionId = sessionInput.value.trim();
  const selected = state.sessions.find((session) => session.sessionId === state.selectedSessionId) || null;
  applySelectedSession(selected);
  await loadSelectedSnapshot();
});

window.addEventListener("beforeunload", () => {
  closeSocket();
  closeOverviewSocket();
});

fitAddon.fit();
updateTerminalSizeDisplay();
updateButtons();
setStates("disconnected", "observer", "unknown");
renderSelectedSession();
refreshHost();
listSessions();
