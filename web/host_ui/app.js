(async () => {
  const hostInfo = document.getElementById("host-info");
  const pendingList = document.getElementById("pending-list");
  const approveResult = document.getElementById("approve-result");
  const displayName = document.getElementById("display-name");
  const sessionsList = document.getElementById("sessions-list");
  const clientsList = document.getElementById("clients-list");
  const events = document.getElementById("events");

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

  function renderSessions(sessions) {
    if (!Array.isArray(sessions) || sessions.length === 0) {
      sessionsList.textContent = "No sessions.";
      return;
    }

    const sortedSessions = [...sessions].sort((left, right) => {
      const leftActive = left.status === "Running" || left.status === "AwaitingInput";
      const rightActive = right.status === "Running" || right.status === "AwaitingInput";
      if (leftActive !== rightActive) {
        return leftActive ? -1 : 1;
      }
      return left.sessionId.localeCompare(right.sessionId);
    });

    sessionsList.innerHTML = "";
    for (const session of sortedSessions) {
      const card = document.createElement("div");
      card.className = "card";

      const title = document.createElement("h3");
      const sessionStateLabel =
        session.status === "Running" || session.status === "AwaitingInput"
          ? "active"
          : "inactive";
      title.textContent = `${session.sessionId} | ${session.title || "(untitled)"} | ${sessionStateLabel}`;

      const meta = document.createElement("div");
      meta.className = "meta";
      meta.textContent =
        `provider: ${session.provider}\nworkspace: ${session.workspaceRoot}\nstatus: ${session.status}\ncontroller: ${session.controllerKind}`;

      const actions = document.createElement("div");
      actions.className = "inline-actions";

      const attachButton = document.createElement("button");
      attachButton.textContent = "Attach";
      attachButton.addEventListener("click", () => {
        window.open(`/ui/terminal?sessionId=${encodeURIComponent(session.sessionId)}`, "_blank", "noopener");
      });

      const stopButton = document.createElement("button");
      stopButton.textContent = "Stop";
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
      card.append(title, meta, actions);
      sessionsList.appendChild(card);
    }
  }

  function renderClients(clients) {
    if (!Array.isArray(clients) || clients.length === 0) {
      clientsList.textContent = "No attached clients.";
      return;
    }

    clientsList.innerHTML = "";
    for (const client of clients) {
      const card = document.createElement("div");
      card.className = "card";

      const title = document.createElement("h3");
      title.textContent = `${client.clientId} | ${client.sessionId}`;

      const meta = document.createElement("div");
      meta.className = "meta";
      meta.textContent =
        `address: ${client.clientAddress}\nclaimed kind: ${client.claimedKind}\nlocal: ${client.isLocal}\ncontrol: ${client.hasControl}`;

      const actions = document.createElement("div");
      actions.className = "inline-actions";

      const disconnectButton = document.createElement("button");
      disconnectButton.textContent = "Disconnect";
      disconnectButton.addEventListener("click", async () => {
        try {
          await fetchJson(`/host/clients/${client.clientId}/disconnect`, { method: "POST" });
          log(`disconnected client ${client.clientId}`);
          await refreshClients();
          await refreshSessions();
        } catch (error) {
          log(`disconnect failed for ${client.clientId}: ${String(error)}`);
        }
      });

      actions.appendChild(disconnectButton);
      card.append(title, meta, actions);
      clientsList.appendChild(card);
    }
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
    renderSessions(await fetchJson("/host/sessions"));
  }

  async function refreshClients() {
    renderClients(await fetchJson("/host/clients"));
  }

  async function refreshAll() {
    try {
      await Promise.all([refreshHost(), refreshPairings(), refreshSessions(), refreshClients()]);
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
