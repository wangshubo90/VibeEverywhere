(async () => {
  const hostInfo = document.getElementById("host-info");
  const hostOverview = document.getElementById("host-overview");
  const hostStatusGrid = document.getElementById("host-status-grid");
  const pendingList = document.getElementById("pending-list");
  const pendingActions = document.getElementById("pending-actions");
  const approveResult = document.getElementById("approve-result");
  const displayName = document.getElementById("display-name");
  const adminHost = document.getElementById("admin-host");
  const adminPort = document.getElementById("admin-port");
  const remoteHost = document.getElementById("remote-host");
  const remotePort = document.getElementById("remote-port");
  const codexCommand = document.getElementById("codex-command");
  const claudeCommand = document.getElementById("claude-command");
  const sessionsList = document.getElementById("sessions-list");
  const fileInspector = document.getElementById("file-inspector");
  const clientsList = document.getElementById("clients-list");
  const clientsSummary = document.getElementById("clients-summary");
  const events = document.getElementById("events");
  const state = {
    sessions: [],
    clients: [],
    inspectedSessionId: "",
    inspectedSessionTitle: "",
    recentFiles: [],
    inspectedFilePath: "",
    inspectedFileContent: "",
    inspectedFileTruncated: false,
    inspectedFileSizeBytes: 0
  };
  let overviewSocket = null;
  let overviewReconnectTimer = null;

  function log(message) {
    const stamp = new Date().toLocaleTimeString();
    events.textContent += `[${stamp}] ${message}\n`;
    events.scrollTop = events.scrollHeight;
  }

  async function fetchJson(path, options = {}) {
    const response = await fetch(path, options);
    const text = await response.text();
    const payload = text ? JSON.parse(text) : null;
    if (!response.ok) {
      throw new Error(typeof payload === "string" ? payload : JSON.stringify(payload));
    }
    return payload;
  }

  function renderJson(element, payload) {
    element.textContent = JSON.stringify(payload, null, 2);
  }

  function decodeBase64ToString(encoded) {
    const binary = atob(encoded || "");
    const bytes = Uint8Array.from(binary, (char) => char.charCodeAt(0));
    return new TextDecoder().decode(bytes);
  }

  async function approvePairing(pairingId, code) {
    try {
      const payload = await fetchJson("/pairing/approve", {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify({ pairingId, code })
      });
      renderJson(approveResult, payload);
      log(`approved pairing for ${payload.deviceId}`);
      await refreshAll();
    } catch (error) {
      approveResult.textContent = String(error);
      log(`approve failed: ${String(error)}`);
    }
  }

  function renderPendingPairings(pairings) {
    pendingActions.innerHTML = "";
    if (!Array.isArray(pairings) || pairings.length === 0) {
      const empty = document.createElement("div");
      empty.className = "empty-note";
      empty.textContent = "No pending pairings.";
      pendingActions.appendChild(empty);
      return;
    }

    for (const pairing of pairings) {
      const card = document.createElement("div");
      card.className = "card";

      const title = document.createElement("h3");
      title.textContent = pairing.deviceName || pairing.pairingId;

      const meta = document.createElement("div");
      meta.className = "detail-list compact";
      appendDetail(meta, "Pairing", pairing.pairingId || "n/a");
      appendDetail(meta, "Type", pairing.deviceType || "unknown");
      appendDetail(meta, "Code", pairing.code || "n/a");
      appendDetail(meta, "Requested", formatTimestamp(pairing.requestedAtUnixMs));

      const actions = document.createElement("div");
      actions.className = "inline-actions";
      const button = document.createElement("button");
      button.textContent = "Approve";
      button.addEventListener("click", () => approvePairing(pairing.pairingId, pairing.code));
      actions.appendChild(button);

      card.append(title, meta, actions);
      pendingActions.appendChild(card);
    }
  }

  function parseCommand(text) {
    const trimmed = text.trim();
    if (!trimmed) {
      return [];
    }
    return trimmed.split(/\s+/).filter(Boolean);
  }

  function formatCommand(command) {
    return Array.isArray(command) ? command.join(" ") : "";
  }

  function formatTimestamp(unixMs) {
    if (typeof unixMs !== "number" || !Number.isFinite(unixMs) || unixMs <= 0) {
      return "n/a";
    }
    return new Date(unixMs).toLocaleString();
  }

  function isArchivedRecord(session) {
    return Boolean(session && (session.archivedRecord ?? session.isRecovered));
  }

  function bucketSession(session) {
    if (session.inventoryState) {
      return session.inventoryState;
    }
    if (session.isActive) {
      return "live";
    }
    if (isArchivedRecord(session)) {
      return "archived";
    }
    return "ended";
  }

  function sessionSortKey(session) {
    const bucketOrder = { live: 0, ended: 1, archived: 2 };
    return [
      bucketOrder[bucketSession(session)] ?? 9,
      -(session.lastStatusAtUnixMs ?? 0),
      -(session.createdAtUnixMs ?? 0),
      session.sessionId
    ];
  }

  function compareKeys(left, right) {
    for (let index = 0; index < left.length; index += 1) {
      if (left[index] < right[index]) {
        return -1;
      }
      if (left[index] > right[index]) {
        return 1;
      }
    }
    return 0;
  }

  function describeController(session) {
    if (session.controllerKind === "remote") {
      return session.controllerClientId
        ? `Remote controller: ${session.controllerClientId}`
        : "Remote controller";
    }
    if (session.controllerKind === "host") {
      return session.controllerClientId
        ? `Host controller: ${session.controllerClientId}`
        : "Host terminal";
    }
    return "No active controller";
  }

  function describeClientRole(client) {
    if (client.hasControl) {
      return client.claimedKind === "host" ? "Host controller" : "Remote controller";
    }
    if (client.claimedKind === "host") {
      return "Host observer";
    }
    if (client.claimedKind === "remote") {
      return "Remote observer";
    }
    return "Observer";
  }

  function canStopSession(session) {
    return !isArchivedRecord(session) && session.status !== "Exited" && session.status !== "Error";
  }

  function describeAttention(session) {
    switch (session.attentionReason) {
      case "awaiting_input":
        return { label: "needs input", tone: "warn" };
      case "session_error":
        return { label: "needs review", tone: "danger" };
      case "workspace_changed":
        return { label: "workspace changed", tone: "good" };
      case "git_state_changed":
        return { label: "git changed", tone: "good" };
      case "controller_changed":
        return { label: "controller changed", tone: "muted" };
      case "session_exited_cleanly":
        return { label: "ended", tone: "muted" };
      default:
        return { label: "", tone: "muted" };
    }
  }

  function makeBadge(label, tone = "neutral") {
    const badge = document.createElement("span");
    badge.className = `badge ${tone}`;
    badge.textContent = label;
    return badge;
  }

  function appendDetail(container, label, value) {
    const row = document.createElement("div");
    const labelElement = document.createElement("span");
    labelElement.textContent = label;
    const valueElement = document.createElement("strong");
    valueElement.textContent = value;
    row.append(labelElement, valueElement);
    container.appendChild(row);
  }

  function resetFileInspector(message = "Select a session file to inspect it here.") {
    state.inspectedSessionId = "";
    state.inspectedSessionTitle = "";
    state.recentFiles = [];
    state.inspectedFilePath = "";
    state.inspectedFileContent = "";
    state.inspectedFileTruncated = false;
    state.inspectedFileSizeBytes = 0;
    fileInspector.innerHTML = "";
    const empty = document.createElement("div");
    empty.className = "empty-note";
    empty.textContent = message;
    fileInspector.appendChild(empty);
  }

  async function loadSessionFiles(session) {
    try {
      const snapshot = await fetchJson(`/sessions/${encodeURIComponent(session.sessionId)}/snapshot`);
      const recentFiles = Array.isArray(snapshot.recentFileChanges) ? snapshot.recentFileChanges : [];
      state.inspectedSessionId = session.sessionId;
      state.inspectedSessionTitle = session.title || "(untitled)";
      state.recentFiles = recentFiles;
      state.inspectedFilePath = "";
      state.inspectedFileContent = "";
      state.inspectedFileTruncated = false;
      state.inspectedFileSizeBytes = 0;
      renderFileInspector();
      log(`loaded recent files for ${session.sessionId}`);
    } catch (error) {
      resetFileInspector(`Failed to load recent files: ${String(error)}`);
      log(`load recent files failed for ${session.sessionId}: ${String(error)}`);
    }
  }

  async function inspectFile(sessionId, workspacePath) {
    try {
      const payload = await fetchJson(
        `/sessions/${encodeURIComponent(sessionId)}/file?path=${encodeURIComponent(workspacePath)}`
      );
      state.inspectedFilePath = payload.workspacePath || workspacePath;
      state.inspectedFileContent = decodeBase64ToString(payload.contentBase64 || "");
      state.inspectedFileTruncated = Boolean(payload.truncated);
      state.inspectedFileSizeBytes = Number(payload.sizeBytes || 0);
      renderFileInspector();
      log(`loaded file ${workspacePath} from ${sessionId}`);
    } catch (error) {
      log(`file read failed for ${workspacePath}: ${String(error)}`);
    }
  }

  function renderFileInspector() {
    fileInspector.innerHTML = "";

    if (!state.inspectedSessionId) {
      resetFileInspector();
      return;
    }

    const heading = document.createElement("div");
    heading.className = "subhead";
    heading.textContent = `${state.inspectedSessionTitle} · ${state.inspectedSessionId}`;
    fileInspector.appendChild(heading);

    const filesBlock = document.createElement("div");
    filesBlock.className = "file-chip-list";
    if (state.recentFiles.length === 0) {
      const empty = document.createElement("div");
      empty.className = "empty-note";
      empty.textContent = "No recent files are available for this session yet.";
      filesBlock.appendChild(empty);
    } else {
      for (const workspacePath of state.recentFiles) {
        const button = document.createElement("button");
        button.className = `file-chip${workspacePath === state.inspectedFilePath ? " selected" : ""}`;
        button.textContent = workspacePath;
        button.addEventListener("click", () => inspectFile(state.inspectedSessionId, workspacePath));
        filesBlock.appendChild(button);
      }
    }
    fileInspector.appendChild(filesBlock);

    if (!state.inspectedFilePath) {
      const empty = document.createElement("div");
      empty.className = "empty-note";
      empty.textContent = "Select one of the recent files to view its current content.";
      fileInspector.appendChild(empty);
      return;
    }

    const meta = document.createElement("div");
    meta.className = "detail-list compact";
    appendDetail(meta, "File", state.inspectedFilePath);
    appendDetail(meta, "Size", `${state.inspectedFileSizeBytes} bytes`);
    appendDetail(meta, "Status", state.inspectedFileTruncated ? "truncated" : "full");
    fileInspector.appendChild(meta);

    const content = document.createElement("pre");
    content.className = "output file-output";
    content.textContent = state.inspectedFileContent;
    fileInspector.appendChild(content);
  }

  async function disconnectClient(client) {
    try {
      await fetchJson(`/host/clients/${client.clientId}/disconnect`, { method: "POST" });
      log(`disconnected client ${client.clientId}`);
      await Promise.all([refreshClients(), refreshSessions()]);
    } catch (error) {
      log(`disconnect failed for ${client.clientId}: ${String(error)}`);
    }
  }

  function renderSessionClient(client) {
    const row = document.createElement("div");
    row.className = "client-row";

    const details = document.createElement("div");
    details.className = "client-row-details";

    const heading = document.createElement("div");
    heading.className = "client-row-heading";
    heading.textContent = client.clientId;

    const meta = document.createElement("div");
    meta.className = "detail-list compact";
    appendDetail(meta, "Role", describeClientRole(client));
    appendDetail(meta, "Address", client.clientAddress);
    appendDetail(meta, "Location", client.isLocal ? "local" : "remote");
    appendDetail(meta, "Connected", formatTimestamp(client.connectedAtUnixMs));

    details.append(heading, meta);

    const button = document.createElement("button");
    button.textContent = "Disconnect";
    button.addEventListener("click", () => disconnectClient(client));

    row.append(details, button);
    return row;
  }

  function renderSessionCard(session, sessionClients) {
    const card = document.createElement("article");
    card.className = `card session-card state-${bucketSession(session)}`;

    const header = document.createElement("div");
    header.className = "session-head";

    const titleBlock = document.createElement("div");
    const title = document.createElement("h3");
    title.textContent = session.title || "(untitled)";
    const subtitle = document.createElement("div");
    subtitle.className = "session-subtitle";
    subtitle.textContent = `${session.sessionId} · ${session.provider}`;
    titleBlock.append(title, subtitle);

    const badges = document.createElement("div");
    badges.className = "badge-row";
    const attention = describeAttention(session);
    badges.append(
      makeBadge(session.status, session.status === "Error" ? "danger" : "neutral"),
      makeBadge(isArchivedRecord(session) ? "archived record" : session.isActive ? "live" : "ended",
                isArchivedRecord(session) ? "warn" : session.isActive ? "good" : "muted"),
      ...(attention.label ? [makeBadge(attention.label, attention.tone)] : []),
      makeBadge(`${session.attachedClientCount ?? sessionClients.length} client${(session.attachedClientCount ?? sessionClients.length) === 1 ? "" : "s"}`)
    );
    header.append(titleBlock, badges);

    const details = document.createElement("div");
    details.className = "detail-list";
    appendDetail(details, "Workspace", session.workspaceRoot);
    appendDetail(details, "Conversation", session.conversationId || "fresh");
    appendDetail(details, "Control", describeController(session));
    appendDetail(details, "Created", formatTimestamp(session.createdAtUnixMs));
    appendDetail(details, "Last lifecycle change", formatTimestamp(session.lastStatusAtUnixMs));
    appendDetail(details, "Last output", formatTimestamp(session.lastOutputAtUnixMs));
    appendDetail(details, "Last activity", formatTimestamp(session.lastActivityAtUnixMs));
    appendDetail(details, "Recent file changes", String(session.recentFileChangeCount ?? 0));
    appendDetail(details, "Git", session.gitDirty ? `dirty (${session.gitBranch || 'unknown'})` : (session.gitBranch || "clean"));

    const actions = document.createElement("div");
    actions.className = "inline-actions";

    const attachButton = document.createElement("button");
    attachButton.textContent = "Attach";
    attachButton.addEventListener("click", () => {
      window.open(`/ui/terminal?sessionId=${encodeURIComponent(session.sessionId)}`, "_blank", "noopener");
    });

    const stopButton = document.createElement("button");
    stopButton.textContent = canStopSession(session) ? "Stop" : "Ended";
    stopButton.disabled = !canStopSession(session);
    stopButton.addEventListener("click", async () => {
      try {
        await fetchJson(`/host/sessions/${session.sessionId}/stop`, { method: "POST" });
        log(`stopped session ${session.sessionId}`);
        await refreshSessions();
      } catch (error) {
        log(`stop failed for ${session.sessionId}: ${String(error)}`);
      }
    });

    const filesButton = document.createElement("button");
    filesButton.textContent = "Files";
    filesButton.addEventListener("click", () => loadSessionFiles(session));

    actions.append(attachButton, filesButton, stopButton);

    const clientsBlock = document.createElement("div");
    clientsBlock.className = "client-block";
    const clientsTitle = document.createElement("div");
    clientsTitle.className = "subhead";
    clientsTitle.textContent = "Attached clients";
    clientsBlock.appendChild(clientsTitle);

    if (sessionClients.length === 0) {
      const empty = document.createElement("div");
      empty.className = "empty-note";
      empty.textContent = isArchivedRecord(session)
        ? "Archived record only. No live clients can still be attached."
        : (session.attachedClientCount ?? 0) > 0
          ? "Refreshing attached client details..."
          : "No clients are currently attached.";
      clientsBlock.appendChild(empty);
    } else {
      const list = document.createElement("div");
      list.className = "client-list-inline";
      for (const client of sessionClients) {
        list.appendChild(renderSessionClient(client));
      }
      clientsBlock.appendChild(list);
    }

    card.append(header, details, actions, clientsBlock);
    return card;
  }

  function renderSessionSection(titleText, sessions, clientsBySession) {
    const section = document.createElement("section");
    section.className = "session-section";

    const title = document.createElement("div");
    title.className = "subhead";
    title.textContent = `${titleText} (${sessions.length})`;
    section.appendChild(title);

    if (sessions.length === 0) {
      const empty = document.createElement("div");
      empty.className = "empty-note";
      empty.textContent = "None.";
      section.appendChild(empty);
      return section;
    }

    const list = document.createElement("div");
    list.className = "list";
    for (const session of sessions) {
      list.appendChild(renderSessionCard(session, clientsBySession.get(session.sessionId) || []));
    }
    section.appendChild(list);
    return section;
  }

  function renderSessions() {
    if (!Array.isArray(state.sessions) || state.sessions.length === 0) {
      sessionsList.textContent = "No sessions.";
      return;
    }

    const clientsBySession = new Map();
    for (const client of state.clients) {
      const bucket = clientsBySession.get(client.sessionId) || [];
      bucket.push(client);
      clientsBySession.set(client.sessionId, bucket);
    }
    for (const clients of clientsBySession.values()) {
      clients.sort((left, right) =>
        compareKeys(
          [left.hasControl ? 0 : 1, left.isLocal ? 0 : 1, -(left.connectedAtUnixMs ?? 0), left.clientId],
          [right.hasControl ? 0 : 1, right.isLocal ? 0 : 1, -(right.connectedAtUnixMs ?? 0), right.clientId]
        ));
    }

    const sessions = [...state.sessions].sort((left, right) =>
      compareKeys(sessionSortKey(left), sessionSortKey(right)));
    const live = sessions.filter((session) => bucketSession(session) === "live");
    const archived = sessions.filter((session) => bucketSession(session) === "archived");
    const ended = sessions.filter((session) => bucketSession(session) === "ended");

    sessionsList.innerHTML = "";

    const summary = document.createElement("div");
    summary.className = "summary-row";
    summary.append(
      makeBadge(`${live.length} live`, live.length > 0 ? "good" : "muted"),
      makeBadge(`${ended.length} ended`, ended.length > 0 ? "neutral" : "muted"),
      makeBadge(`${archived.length} archived`, archived.length > 0 ? "warn" : "muted")
    );
    sessionsList.appendChild(summary);
    sessionsList.append(
      renderSessionSection("Live", live, clientsBySession),
      renderSessionSection("Ended", ended, clientsBySession),
      renderSessionSection("Archived Records", archived, clientsBySession)
    );
  }

  function renderClients() {
    if (!Array.isArray(state.clients) || state.clients.length === 0) {
      clientsSummary.innerHTML = "";
      clientsSummary.append(
        makeBadge("0 controllers", "muted"),
        makeBadge("0 remote", "muted"),
        makeBadge("0 local", "muted")
      );
      clientsList.textContent = "No attached clients.";
      return;
    }

    const clients = [...state.clients].sort((left, right) =>
      compareKeys(
        [left.sessionId, left.hasControl ? 0 : 1, left.clientId],
        [right.sessionId, right.hasControl ? 0 : 1, right.clientId]
      ));

    clientsList.innerHTML = "";
    const controllers = clients.filter((client) => client.hasControl).length;
    const local = clients.filter((client) => client.isLocal).length;
    const remote = clients.length - local;
    clientsSummary.innerHTML = "";
    clientsSummary.append(
      makeBadge(`${controllers} controller${controllers === 1 ? "" : "s"}`, controllers > 0 ? "warn" : "muted"),
      makeBadge(`${remote} remote`, remote > 0 ? "neutral" : "muted"),
      makeBadge(`${local} local`, local > 0 ? "good" : "muted")
    );
    for (const client of clients) {
      const card = document.createElement("div");
      card.className = "card";

      const title = document.createElement("h3");
      title.textContent = client.clientId;

      const meta = document.createElement("div");
      meta.className = "detail-list compact";
      appendDetail(meta, "Session", `${client.sessionId} · ${client.sessionTitle || "(untitled)"}`);
      appendDetail(meta, "Session state",
                   `${client.sessionStatus}${(client.sessionArchivedRecord ?? client.sessionIsRecovered) ? " · archived" : ""}`);
      appendDetail(meta, "Role", describeClientRole(client));
      appendDetail(meta, "Connected", formatTimestamp(client.connectedAtUnixMs));
      appendDetail(meta, "Address", client.clientAddress);
      appendDetail(meta, "Location", client.isLocal ? "local" : "remote");

      const actions = document.createElement("div");
      actions.className = "inline-actions";

      const disconnectButton = document.createElement("button");
      disconnectButton.textContent = "Disconnect";
      disconnectButton.addEventListener("click", () => disconnectClient(client));

      actions.appendChild(disconnectButton);
      card.append(title, meta, actions);
      clientsList.appendChild(card);
    }
  }

  function renderDashboard() {
    renderSessions();
    renderClients();
    renderFileInspector();
  }

  function closeOverviewSocket() {
    if (overviewReconnectTimer !== null) {
      clearTimeout(overviewReconnectTimer);
      overviewReconnectTimer = null;
    }
    if (overviewSocket) {
      overviewSocket.close();
      overviewSocket = null;
    }
  }

  function scheduleOverviewReconnect() {
    if (overviewReconnectTimer !== null) {
      return;
    }
    overviewReconnectTimer = setTimeout(() => {
      overviewReconnectTimer = null;
      connectOverviewSocket();
    }, 1500);
  }

  async function handleOverviewSnapshot(payload) {
    state.sessions = Array.isArray(payload.sessions) ? payload.sessions : [];
    try {
      state.clients = await fetchJson("/host/clients");
    } catch (error) {
      log(`client refresh failed after overview update: ${String(error)}`);
    }
    renderDashboard();
  }

  function connectOverviewSocket() {
    if (overviewSocket &&
        (overviewSocket.readyState === WebSocket.OPEN ||
         overviewSocket.readyState === WebSocket.CONNECTING)) {
      return;
    }

    const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
    overviewSocket = new WebSocket(`${protocol}//${window.location.host}/ws/overview`);

    overviewSocket.addEventListener("open", () => {
      log("overview subscription connected");
    });

    overviewSocket.addEventListener("message", async (event) => {
      try {
        const payload = JSON.parse(event.data);
        if (payload.type === "sessions.snapshot") {
          await handleOverviewSnapshot(payload);
        }
      } catch (error) {
        log(`overview update parse failed: ${String(error)}`);
      }
    });

    overviewSocket.addEventListener("close", () => {
      overviewSocket = null;
      scheduleOverviewReconnect();
    });

    overviewSocket.addEventListener("error", () => {
      log("overview subscription error");
    });
  }

  async function refreshHost() {
    const payload = await fetchJson("/host/info");
    renderJson(hostInfo, payload);
    hostOverview.innerHTML = "";
    hostOverview.append(
      makeBadge(payload.displayName || payload.hostId || "Host", "good"),
      makeBadge(payload.pairingMode || "approval", "neutral"),
      makeBadge(payload.tls?.enabled ? "remote tls on" : "remote tls off",
                payload.tls?.enabled ? "good" : "muted")
    );
    hostStatusGrid.innerHTML = "";
    appendDetail(hostStatusGrid, "Display name", payload.displayName || "n/a");
    appendDetail(hostStatusGrid, "Admin", `${payload.adminHost || "n/a"}:${payload.adminPort || "n/a"}`);
    appendDetail(hostStatusGrid, "Remote", `${payload.remoteHost || "n/a"}:${payload.remotePort || "n/a"}`);
    appendDetail(hostStatusGrid, "Pairing", payload.pairingMode || "approval");
    appendDetail(hostStatusGrid, "TLS", payload.tls?.enabled ? "enabled" : "disabled");
    appendDetail(hostStatusGrid, "Codex override", formatCommand(payload.providerCommands?.codex) || "default");
    appendDetail(hostStatusGrid, "Claude override", formatCommand(payload.providerCommands?.claude) || "default");
    displayName.value = payload.displayName || "";
    adminHost.value = payload.adminHost || "";
    adminPort.value = payload.adminPort || "";
    remoteHost.value = payload.remoteHost || "";
    remotePort.value = payload.remotePort || "";
    codexCommand.value = formatCommand(payload.providerCommands?.codex);
    claudeCommand.value = formatCommand(payload.providerCommands?.claude);
  }

  async function refreshPairings() {
    const payload = await fetchJson("/pairing/pending");
    renderPendingPairings(payload);
    renderJson(pendingList, payload);
  }

  async function refreshSessions() {
    state.sessions = await fetchJson("/host/sessions");
    renderDashboard();
  }

  async function clearInactiveSessions() {
    try {
      const payload = await fetchJson("/host/sessions/clear-inactive", { method: "POST" });
      log(`cleared ${payload.removedCount ?? 0} ended/archive record(s)`);
      await refreshSessions();
    } catch (error) {
      log(`clear ended/archive failed: ${String(error)}`);
    }
  }

  async function refreshClients() {
    state.clients = await fetchJson("/host/clients");
    renderDashboard();
  }

  async function refreshAll() {
    try {
      const [sessions, clients] = await Promise.all([
        fetchJson("/host/sessions"),
        fetchJson("/host/clients"),
        refreshHost(),
        refreshPairings()
      ]);
      state.sessions = sessions;
      state.clients = clients;
      renderDashboard();
      connectOverviewSocket();
      log("refreshed host admin state");
    } catch (error) {
      log(`refresh failed: ${String(error)}`);
    }
  }

  document.getElementById("config-form").addEventListener("submit", async (event) => {
    event.preventDefault();
    try {
      const providerCommands = {};
      const parsedCodexCommand = parseCommand(codexCommand.value);
      const parsedClaudeCommand = parseCommand(claudeCommand.value);
      if (parsedCodexCommand.length > 0) {
        providerCommands.codex = parsedCodexCommand;
      }
      if (parsedClaudeCommand.length > 0) {
        providerCommands.claude = parsedClaudeCommand;
      }

      const payload = await fetchJson("/host/config", {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify({
          displayName: displayName.value,
          adminHost: adminHost.value,
          adminPort: Number(adminPort.value),
          remoteHost: remoteHost.value,
          remotePort: Number(remotePort.value),
          providerCommands
        })
      });
      renderJson(hostInfo, payload);
      log("saved host config");
    } catch (error) {
      log(`save host config failed: ${String(error)}`);
    }
  });

  document.getElementById("approve-form").addEventListener("submit", async (event) => {
    event.preventDefault();
    await approvePairing(document.getElementById("pairing-id").value,
                         document.getElementById("pairing-code").value);
  });

  document.getElementById("refresh-all").addEventListener("click", refreshAll);
  document.getElementById("refresh-pairings").addEventListener("click", refreshPairings);
  document.getElementById("refresh-sessions").addEventListener("click", refreshSessions);
  document.getElementById("clear-inactive-sessions").addEventListener("click", clearInactiveSessions);
  document.getElementById("refresh-clients").addEventListener("click", refreshClients);

  window.addEventListener("beforeunload", closeOverviewSocket);
  await refreshAll();
})();
