(async () => {
  const hostInfo = document.getElementById("host-info");
  const pendingList = document.getElementById("pending-list");
  const approveResult = document.getElementById("approve-result");
  const displayName = document.getElementById("display-name");
  const sessionsList = document.getElementById("sessions-list");
  const clientsList = document.getElementById("clients-list");
  const events = document.getElementById("events");
  const state = {
    sessions: [],
    clients: []
  };

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

  function formatTimestamp(unixMs) {
    if (typeof unixMs !== "number" || !Number.isFinite(unixMs) || unixMs <= 0) {
      return "n/a";
    }
    return new Date(unixMs).toLocaleString();
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

  function sessionSortKey(session) {
    const bucketOrder = { active: 0, recovered: 1, inactive: 2 };
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
    return !session.isRecovered && session.status !== "Exited" && session.status !== "Error";
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
    badges.append(
      makeBadge(session.activityState, session.isActive ? "good" : session.isRecovered ? "warn" : "muted"),
      makeBadge(session.status, session.status === "Error" ? "danger" : "neutral"),
      makeBadge(`${sessionClients.length} client${sessionClients.length === 1 ? "" : "s"}`)
    );
    header.append(titleBlock, badges);

    const details = document.createElement("div");
    details.className = "detail-list";
    appendDetail(details, "Workspace", session.workspaceRoot);
    appendDetail(details, "Control", describeController(session));
    appendDetail(details, "Created", formatTimestamp(session.createdAtUnixMs));
    appendDetail(details, "Last state change", formatTimestamp(session.lastStatusAtUnixMs));

    const actions = document.createElement("div");
    actions.className = "inline-actions";

    const attachButton = document.createElement("button");
    attachButton.textContent = "Attach";
    attachButton.addEventListener("click", () => {
      window.open(`/ui/terminal?sessionId=${encodeURIComponent(session.sessionId)}`, "_blank", "noopener");
    });

    const stopButton = document.createElement("button");
    stopButton.textContent = canStopSession(session) ? "Stop" : "Stopped";
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

    actions.append(attachButton, stopButton);

    const clientsBlock = document.createElement("div");
    clientsBlock.className = "client-block";
    const clientsTitle = document.createElement("div");
    clientsTitle.className = "subhead";
    clientsTitle.textContent = "Attached clients";
    clientsBlock.appendChild(clientsTitle);

    if (sessionClients.length === 0) {
      const empty = document.createElement("div");
      empty.className = "empty-note";
      empty.textContent = session.isRecovered
        ? "Recovered record only. No live clients can still be attached."
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
    const active = sessions.filter((session) => bucketSession(session) === "active");
    const recovered = sessions.filter((session) => bucketSession(session) === "recovered");
    const inactive = sessions.filter((session) => bucketSession(session) === "inactive");

    sessionsList.innerHTML = "";

    const summary = document.createElement("div");
    summary.className = "summary-row";
    summary.append(
      makeBadge(`${active.length} active`, active.length > 0 ? "good" : "muted"),
      makeBadge(`${recovered.length} recovered`, recovered.length > 0 ? "warn" : "muted"),
      makeBadge(`${inactive.length} inactive`, inactive.length > 0 ? "neutral" : "muted")
    );
    sessionsList.appendChild(summary);
    sessionsList.append(
      renderSessionSection("Active", active, clientsBySession),
      renderSessionSection("Recovered", recovered, clientsBySession),
      renderSessionSection("Inactive / Stopped", inactive, clientsBySession)
    );
  }

  function renderClients() {
    if (!Array.isArray(state.clients) || state.clients.length === 0) {
      clientsList.textContent = "No attached clients.";
      return;
    }

    const clients = [...state.clients].sort((left, right) =>
      compareKeys(
        [left.sessionId, left.hasControl ? 0 : 1, left.clientId],
        [right.sessionId, right.hasControl ? 0 : 1, right.clientId]
      ));

    clientsList.innerHTML = "";
    for (const client of clients) {
      const card = document.createElement("div");
      card.className = "card";

      const title = document.createElement("h3");
      title.textContent = client.clientId;

      const meta = document.createElement("div");
      meta.className = "detail-list compact";
      appendDetail(meta, "Session", `${client.sessionId} · ${client.sessionTitle || "(untitled)"}`);
      appendDetail(meta, "Session state",
                   `${client.sessionStatus}${client.sessionIsRecovered ? " · recovered" : ""}`);
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
  }

  async function refreshHost() {
    const payload = await fetchJson("/host/info");
    renderJson(hostInfo, payload);
    displayName.value = payload.displayName || "";
  }

  async function refreshPairings() {
    renderJson(pendingList, await fetchJson("/pairing/pending"));
  }

  async function refreshSessions() {
    state.sessions = await fetchJson("/host/sessions");
    renderDashboard();
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
      log("refreshed host admin state");
    } catch (error) {
      log(`refresh failed: ${String(error)}`);
    }
  }

  document.getElementById("config-form").addEventListener("submit", async (event) => {
    event.preventDefault();
    try {
      const payload = await fetchJson("/host/config", {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify({ displayName: displayName.value })
      });
      renderJson(hostInfo, payload);
      log("saved host config");
    } catch (error) {
      log(`save host config failed: ${String(error)}`);
    }
  });

  document.getElementById("approve-form").addEventListener("submit", async (event) => {
    event.preventDefault();
    try {
      const payload = await fetchJson("/pairing/approve", {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify({
          pairingId: document.getElementById("pairing-id").value,
          code: document.getElementById("pairing-code").value
        })
      });
      renderJson(approveResult, payload);
      log(`approved pairing for ${payload.deviceId}`);
      await refreshAll();
    } catch (error) {
      approveResult.textContent = String(error);
      log(`approve failed: ${String(error)}`);
    }
  });

  document.getElementById("refresh-all").addEventListener("click", refreshAll);
  document.getElementById("refresh-pairings").addEventListener("click", refreshPairings);
  document.getElementById("refresh-sessions").addEventListener("click", refreshSessions);
  document.getElementById("refresh-clients").addEventListener("click", refreshClients);

  await refreshAll();
})();
