import { Terminal } from 'https://cdn.jsdelivr.net/npm/@xterm/xterm/+esm';
import { FitAddon } from 'https://cdn.jsdelivr.net/npm/@xterm/addon-fit/+esm';

const params = new URLSearchParams(window.location.search);
const sessionId = params.get('sessionId') || '';
const statusEl = document.getElementById('status');
const eventsEl = document.getElementById('events');
const sessionLabelEl = document.getElementById('session-label');
const terminalCaptionEl = document.getElementById('terminal-caption');
const terminalSizeEl = document.getElementById('terminal-size');
const requestControlBtn = document.getElementById('request-control');
const releaseControlBtn = document.getElementById('release-control');
const stopSessionBtn = document.getElementById('stop-session');

const terminal = new Terminal({
  convertEol: false,
  cursorBlink: true,
  fontFamily: 'Iosevka Comfy, SF Mono, Menlo, monospace',
  fontSize: 14,
  scrollback: 5000,
  theme: {
    background: '#161513',
    foreground: '#efe7d7',
    cursor: '#d96c3f'
  }
});
const fitAddon = new FitAddon();
terminal.loadAddon(fitAddon);
terminal.open(document.getElementById('terminal'));
fitAddon.fit();

let ws = null;
let token = '';
let hasControl = false;
let requestedControl = false;
let sessionStatus = 'unknown';
let activeControllerKind = 'none';
let activeControllerHasClient = false;

function log(message) {
  const stamp = new Date().toLocaleTimeString();
  eventsEl.textContent += `[${stamp}] ${message}\n`;
  eventsEl.scrollTop = eventsEl.scrollHeight;
}

function setStatus(message) {
  statusEl.textContent = message;
}

function updateTerminalSize() {
  terminalSizeEl.textContent = `${terminal.cols} x ${terminal.rows}`;
}

function websocketUrl() {
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  return `${protocol}//${window.location.host}/ws/sessions/${encodeURIComponent(sessionId)}?access_token=${encodeURIComponent(token)}`;
}

async function fetchJson(path, options = {}) {
  const response = await fetch(path, options);
  const text = await response.text();
  const payload = text ? JSON.parse(text) : null;
  if (!response.ok) {
    throw new Error(typeof payload === 'string' ? payload : JSON.stringify(payload));
  }
  return payload;
}

function decodeBase64ToString(encoded) {
  const binary = atob(encoded);
  const bytes = Uint8Array.from(binary, char => char.charCodeAt(0));
  return new TextDecoder().decode(bytes);
}

function sendJson(payload) {
  if (!ws || ws.readyState !== WebSocket.OPEN) {
    log('websocket not open');
    return;
  }
  ws.send(JSON.stringify(payload));
}

function isInteractiveStatus(status) {
  return status === 'Running' || status === 'AwaitingInput';
}

function maybeRequestHostControl() {
  if (!ws || ws.readyState !== WebSocket.OPEN) {
    return;
  }
  if (!isInteractiveStatus(sessionStatus)) {
    return;
  }
  if (activeControllerKind === 'host' && !activeControllerHasClient && !requestedControl) {
    requestedControl = true;
    log('requesting host control');
    sendJson({ type: 'session.control.request', kind: 'host' });
  }
}

function connect() {
  if (!sessionId) {
    setStatus('missing sessionId');
    return;
  }
  terminal.clear();
  setStatus('connecting');
  sessionLabelEl.textContent = `Session ${sessionId}`;
  terminalCaptionEl.textContent = sessionId;
  ws = new WebSocket(websocketUrl());

  ws.addEventListener('open', () => {
    setStatus('connected');
    fitAddon.fit();
    updateTerminalSize();
    terminal.focus();
    maybeRequestHostControl();
  });

  ws.addEventListener('message', event => {
    const payload = JSON.parse(event.data);
    if (payload.type === 'terminal.output' && payload.dataEncoding === 'base64') {
      terminal.write(decodeBase64ToString(payload.dataBase64 || ''));
      return;
    }

    if (payload.type === 'session.updated') {
      sessionStatus = payload.status || sessionStatus;
      activeControllerKind = payload.controllerKind || activeControllerKind;
      activeControllerHasClient = Boolean(payload.controllerClientId);

      if (requestedControl && activeControllerKind === 'host' && activeControllerHasClient) {
        hasControl = true;
        requestedControl = false;
      } else if (activeControllerKind === 'remote') {
        hasControl = false;
        requestedControl = false;
      } else if (activeControllerKind === 'host' && !activeControllerHasClient) {
        hasControl = false;
      }

      setStatus(`status=${sessionStatus} controller=${activeControllerKind}${activeControllerHasClient ? ' (claimed)' : ''}`);
      maybeRequestHostControl();
      return;
    }

    if (payload.type === 'session.exited') {
      setStatus(`exited (${payload.status})`);
      hasControl = false;
      requestedControl = false;
      return;
    }

    if (payload.type === 'error') {
      if (payload.code === 'command_rejected') {
        requestedControl = false;
      }
      log(`error: ${payload.code} | ${payload.message}`);
      maybeRequestHostControl();
      return;
    }

    log(JSON.stringify(payload));
  });

  ws.addEventListener('close', () => {
    setStatus('disconnected');
    hasControl = false;
    requestedControl = false;
    log('websocket closed');
  });

  ws.addEventListener('error', () => {
    log('websocket error');
  });
}

requestControlBtn.addEventListener('click', () => {
  requestedControl = true;
  sendJson({ type: 'session.control.request', kind: 'host' });
});

releaseControlBtn.addEventListener('click', () => {
  hasControl = false;
  requestedControl = false;
  sendJson({ type: 'session.control.release' });
});

stopSessionBtn.addEventListener('click', () => {
  sendJson({ type: 'session.stop' });
});

terminal.onData(data => {
  if (!hasControl) {
    return;
  }
  sendJson({ type: 'terminal.input', data });
});

window.addEventListener('resize', () => {
  fitAddon.fit();
  updateTerminalSize();
  if (ws && ws.readyState === WebSocket.OPEN && hasControl) {
    sendJson({ type: 'terminal.resize', cols: terminal.cols, rows: terminal.rows });
  }
});

(async () => {
  try {
    const payload = await fetchJson('/host/local-token');
    token = payload.token;
    updateTerminalSize();
    connect();
  } catch (error) {
    setStatus(`failed to bootstrap: ${String(error)}`);
  }
})();
