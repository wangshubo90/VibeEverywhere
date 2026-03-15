import { AfterViewInit, Component, ElementRef, OnDestroy, OnInit, ViewChild } from '@angular/core';
import { FormsModule } from '@angular/forms';
import { RemoteSessionSortMode, SessionSummaryView } from '../../../session-model/src/public-api';
import { attentionTone, inventoryTone } from '../../../shared-ui/src/public-api';
import { FitAddon } from '@xterm/addon-fit';
import { Terminal } from 'xterm';

type ConnectionState = 'disconnected' | 'connecting' | 'connected';
type ControllerState = 'observer' | 'host' | 'remote';

interface HostInfoResponse {
  displayName?: string;
  remoteHost?: string;
  remotePort?: number;
}

interface PairingRequestResponse {
  pairingId: string;
  code: string;
  status: string;
}

interface PairingClaimResponse {
  token?: string;
  deviceId?: string;
  status?: string;
}

interface RawSessionSummary {
  sessionId: string;
  title: string;
  provider: string;
  workspaceRoot?: string;
  status: string;
  inventoryState?: 'live' | 'ended' | 'archived';
  attentionState?: 'none' | 'info' | 'action_required' | 'intervention';
  attentionReason?:
    | 'none'
    | 'awaiting_input'
    | 'session_error'
    | 'workspace_changed'
    | 'git_state_changed'
    | 'controller_changed'
    | 'session_exited_cleanly';
  controllerKind?: 'host' | 'remote' | 'none';
  controllerClientId?: string;
  attachedClientCount?: number;
  conversationId?: string;
  lastActivityAtUnixMs?: number;
  lastOutputAtUnixMs?: number;
  createdAtUnixMs?: number;
  gitDirty?: boolean;
  gitBranch?: string;
  recentFileChangeCount?: number;
}

interface SessionSnapshotResponse {
  sessionId: string;
  title: string;
  provider: string;
  workspaceRoot?: string;
  status: string;
  conversationId?: string;
  currentSequence?: number;
  recentTerminalTail?: string;
  recentFileChanges?: string[];
  git?: {
    branch?: string;
    modifiedCount?: number;
    stagedCount?: number;
    untrackedCount?: number;
  };
  signals?: {
    lastOutputAtUnixMs?: number;
    lastActivityAtUnixMs?: number;
    attentionState?: string;
    attentionReason?: string;
    recentFileChangeCount?: number;
  };
}

interface SessionFileResponse {
  workspacePath: string;
  contentBase64: string;
  contentEncoding: string;
  sizeBytes: number;
  truncated: boolean;
}

interface RemoteSessionTab {
  sessionId: string;
  title: string;
  connectionState: ConnectionState;
  controllerState: ControllerState;
  terminalOutput: string;
  lastEvent: string;
}

type RemoteViewTab = 'pairing' | 'create' | 'sessions';
type SessionDetailTab = 'overview' | 'files';

@Component({
  selector: 'app-root',
  standalone: true,
  imports: [FormsModule],
  templateUrl: './app.html',
  styleUrl: './app.css',
})
export class App implements OnInit, OnDestroy, AfterViewInit {
  @ViewChild('terminalHost') terminalHost?: ElementRef<HTMLDivElement>;
  activeViewTab: RemoteViewTab = 'pairing';
  activeSessionDetailTab: SessionDetailTab = 'overview';

  readonly sortModes: Array<{ value: RemoteSessionSortMode; label: string }> = [
    { value: 'attention', label: 'Attention' },
    { value: 'recent-activity', label: 'Recent activity' },
    { value: 'recent-output', label: 'Recent output' },
    { value: 'created', label: 'Created time' },
    { value: 'title', label: 'Title' },
    { value: 'provider', label: 'Provider' },
  ];

  host = this.defaultHost();
  port = this.defaultPort();
  token = this.loadStored('token', '');
  provider = this.loadStored('provider', 'codex');
  title = this.loadStored('title', 'smoke-session');
  workspaceRoot = this.loadStored('workspaceRoot', '.');
  conversationId = this.loadStored('conversationId', '');
  command = this.loadStored('command', '');
  sortMode: RemoteSessionSortMode = (this.loadStored('sortMode', 'attention') as RemoteSessionSortMode) || 'attention';

  hostInfoText = 'No host info loaded.';
  pairingText = 'No pairing request started.';
  events: string[] = [];

  sessions: SessionSummaryView[] = [];
  selectedSessionId = this.loadStored('selectedSessionId', '');
  selectedSession: SessionSummaryView | null = null;
  selectedSnapshot: SessionSnapshotResponse | null = null;
  selectedFilePath = '';
  selectedFileContent = 'Select a recent file to read its current content.';
  selectedFileSizeBytes = 0;
  selectedFileTruncated = false;

  openTabs: RemoteSessionTab[] = [];
  activeTabSessionId = '';

  overviewConnection: ConnectionState = 'disconnected';
  pairingPollTimer: ReturnType<typeof setTimeout> | null = null;
  overviewReconnectTimer: ReturnType<typeof setTimeout> | null = null;
  overviewSocket: WebSocket | null = null;
  sessionSockets = new Map<string, WebSocket>();
  terminal: Terminal | null = null;
  fitAddon: FitAddon | null = null;
  resizeObserver: ResizeObserver | null = null;

  protected readonly inventoryTone = inventoryTone;
  protected readonly attentionTone = attentionTone;

  ngOnInit(): void {
    this.restoreOpenTabs();
    if (this.selectedSessionId || this.openTabs.length > 0) {
      this.activeViewTab = 'sessions';
    }
    void this.refreshHost();
    void this.loadSessions();
    this.connectOverviewSocket();
  }

  ngAfterViewInit(): void {
    this.initializeTerminal();
  }

  ngOnDestroy(): void {
    if (this.pairingPollTimer !== null) {
      clearTimeout(this.pairingPollTimer);
    }
    if (this.overviewReconnectTimer !== null) {
      clearTimeout(this.overviewReconnectTimer);
    }
    if (this.overviewSocket) {
      this.overviewSocket.close();
      this.overviewSocket = null;
    }
    for (const socket of this.sessionSockets.values()) {
      socket.close();
    }
    this.sessionSockets.clear();
    if (this.resizeObserver) {
      this.resizeObserver.disconnect();
      this.resizeObserver = null;
    }
    this.terminal?.dispose();
    this.terminal = null;
  }

  get sortedSessions(): SessionSummaryView[] {
    const sessions = [...this.sessions];
    sessions.sort((left, right) => {
      switch (this.sortMode) {
        case 'title':
          return left.title.localeCompare(right.title);
        case 'provider':
          return left.provider.localeCompare(right.provider);
        case 'created':
          return left.sessionId.localeCompare(right.sessionId, undefined, { numeric: true });
        case 'recent-output':
          return this.compareRelativeLabel(left.lastOutputLabel, right.lastOutputLabel);
        case 'recent-activity':
          return this.compareRelativeLabel(left.lastActivityLabel, right.lastActivityLabel);
        case 'attention':
        default: {
          const severity = (session: SessionSummaryView): number => {
            if (session.attentionState === 'intervention') {
              return 0;
            }
            if (session.attentionState === 'action_required') {
              return 1;
            }
            if (session.inventoryState === 'live') {
              return 2;
            }
            if (session.inventoryState === 'ended') {
              return 3;
            }
            return 4;
          };
          const rank = severity(left) - severity(right);
          if (rank !== 0) {
            return rank;
          }
          return left.sessionId.localeCompare(right.sessionId, undefined, { numeric: true });
        }
      }
    });
    return sessions;
  }

  get activeTab(): RemoteSessionTab | null {
    return this.openTabs.find((tab) => tab.sessionId === this.activeTabSessionId) ?? null;
  }

  selectViewTab(tab: RemoteViewTab): void {
    this.activeViewTab = tab;
  }

  selectSessionDetailTab(tab: SessionDetailTab): void {
    this.activeSessionDetailTab = tab;
  }

  get connectionStateLabel(): string {
    return this.activeTab?.connectionState ?? this.overviewConnection;
  }

  get controllerStateLabel(): string {
    return this.activeTab?.controllerState ?? (this.selectedSession?.controllerKind ?? 'observer');
  }

  get sessionStateLabel(): string {
    return this.selectedSession?.lifecycleStatus ?? 'unknown';
  }

  async refreshHost(): Promise<void> {
    try {
      const payload = await this.fetchJson<HostInfoResponse>('/host/info');
      this.hostInfoText = JSON.stringify(payload, null, 2);
      this.log('loaded host info');
    } catch (error) {
      this.hostInfoText = String(error);
      this.log(`host info failed: ${String(error)}`);
    }
  }

  async startPairing(): Promise<void> {
    try {
      const payload = await this.fetchJson<PairingRequestResponse>('/pairing/request', {
        method: 'POST',
        body: JSON.stringify({ deviceName: 'Angular Remote Client', deviceType: 'browser' }),
      });
      this.pairingText = JSON.stringify(payload, null, 2);
      this.log(`pairing requested: ${payload.pairingId}`);
      this.pollPairingClaim(payload.pairingId, payload.code);
    } catch (error) {
      this.pairingText = String(error);
      this.log(`pairing request failed: ${String(error)}`);
    }
  }

  saveToken(): void {
    this.persistSettings();
    this.log('saved token and connection settings');
    this.connectOverviewSocket();
  }

  clearToken(): void {
    this.token = '';
    this.persistSettings();
    this.disconnectOverviewSocket();
    this.log('cleared saved token');
  }

  async loadSessions(): Promise<void> {
    try {
      const payload = await this.fetchJson<RawSessionSummary[]>('/sessions');
      this.applySessions(payload);
      this.log(`loaded ${this.sessions.length} sessions`);
    } catch (error) {
      this.log(`load sessions failed: ${String(error)}`);
    }
  }

  async createSession(): Promise<void> {
    try {
      const payload = {
        provider: this.provider,
        workspaceRoot: this.workspaceRoot.trim(),
        title: this.title.trim() || 'smoke-session',
        ...(this.conversationId.trim() ? { conversationId: this.conversationId.trim() } : {}),
        ...(this.parseCommand(this.command).length > 0 ? { command: this.parseCommand(this.command) } : {}),
      };
      const created = await this.fetchJson<{ sessionId: string }>('/sessions', {
        method: 'POST',
        body: JSON.stringify(payload),
      });
      this.selectedSessionId = created.sessionId;
      this.persistSettings();
      this.log(`created session ${created.sessionId}`);
      await this.loadSessions();
      await this.selectSession(created.sessionId);
      this.activeViewTab = 'sessions';
    } catch (error) {
      this.log(`create session failed: ${String(error)}`);
    }
  }

  async selectSession(sessionId: string): Promise<void> {
    this.activeViewTab = 'sessions';
    this.selectedSessionId = sessionId;
    this.selectedSession = this.sessions.find((session) => session.sessionId === sessionId) ?? null;
    this.selectedSnapshot = null;
    this.selectedFilePath = '';
    this.selectedFileContent = 'Select a recent file to read its current content.';
    this.selectedFileSizeBytes = 0;
    this.selectedFileTruncated = false;
    this.persistSettings();
    if (this.selectedSession) {
      await this.loadSnapshot(sessionId);
      this.log(`selected session ${sessionId}`);
    }
    this.refreshTerminal();
  }

  openSessionTab(sessionId: string): void {
    const session = this.sessions.find((item) => item.sessionId === sessionId);
    if (!session) {
      return;
    }
    if (!this.openTabs.some((tab) => tab.sessionId === sessionId)) {
      this.openTabs.push({
        sessionId,
        title: session.title,
        connectionState: 'disconnected',
        controllerState: 'observer',
        terminalOutput: '',
        lastEvent: 'Tab opened.',
      });
    }
    this.activeTabSessionId = sessionId;
    this.activeViewTab = 'sessions';
    void this.selectSession(sessionId);
    this.persistSettings();
  }

  closeSessionTab(sessionId: string): void {
    this.disconnectTab(sessionId);
    this.openTabs = this.openTabs.filter((tab) => tab.sessionId !== sessionId);
    if (this.activeTabSessionId === sessionId) {
      this.activeTabSessionId = this.openTabs[0]?.sessionId ?? '';
    }
    this.refreshTerminal();
    this.persistSettings();
  }

  activateTab(sessionId: string): void {
    this.activeTabSessionId = sessionId;
    void this.selectSession(sessionId);
    this.refreshTerminal();
    this.persistSettings();
  }

  connectActiveTab(): void {
    if (!this.activeTab) {
      if (this.selectedSession) {
        this.openSessionTab(this.selectedSession.sessionId);
      }
    }
    const tab = this.activeTab;
    if (!tab) {
      return;
    }
    if (this.sessionSockets.has(tab.sessionId)) {
      return;
    }

    const socket = new WebSocket(this.sessionWebsocketUrl(tab.sessionId));
    this.sessionSockets.set(tab.sessionId, socket);
    tab.connectionState = 'connecting';
    tab.lastEvent = 'Connecting…';

    socket.addEventListener('open', () => {
      tab.connectionState = 'connected';
      tab.lastEvent = 'WebSocket open.';
      this.log(`connected session websocket ${tab.sessionId}`);
      this.fitTerminal();
      this.sendResizeForTab(tab.sessionId);
      this.persistSettings();
    });

    socket.addEventListener('message', (event) => {
      this.handleSessionMessage(tab.sessionId, String(event.data));
    });

    socket.addEventListener('close', (event) => {
      this.sessionSockets.delete(tab.sessionId);
      tab.connectionState = 'disconnected';
      tab.lastEvent = `WebSocket closed (${event.code})`;
      this.log(`session websocket closed ${tab.sessionId} (${event.code})`);
      this.persistSettings();
    });

    socket.addEventListener('error', () => {
      tab.lastEvent = 'WebSocket error.';
      this.log(`session websocket error ${tab.sessionId}`);
    });
  }

  disconnectActiveTab(): void {
    if (this.activeTab) {
      this.disconnectTab(this.activeTab.sessionId);
    }
  }

  requestControl(): void {
    if (!this.activeTab) {
      return;
    }
    this.sendSessionCommand(this.activeTab.sessionId, { type: 'session.control.request', kind: 'remote' });
  }

  releaseControl(): void {
    if (!this.activeTab) {
      return;
    }
    this.sendSessionCommand(this.activeTab.sessionId, { type: 'session.control.release' });
  }

  stopSession(): void {
    if (!this.activeTab) {
      return;
    }
    this.sendSessionCommand(this.activeTab.sessionId, { type: 'session.stop' });
  }

  async inspectFile(workspacePath: string): Promise<void> {
    if (!this.selectedSessionId) {
      return;
    }
    try {
      const payload = await this.fetchJson<SessionFileResponse>(
        `/sessions/${encodeURIComponent(this.selectedSessionId)}/file?path=${encodeURIComponent(workspacePath)}`,
      );
      this.selectedFilePath = payload.workspacePath;
      this.selectedFileContent = this.decodeBase64(payload.contentBase64);
      this.selectedFileSizeBytes = payload.sizeBytes;
      this.selectedFileTruncated = payload.truncated;
    } catch (error) {
      this.log(`file inspect failed: ${String(error)}`);
    }
  }

  attentionLabel(session: SessionSummaryView): string {
    return session.attentionReason === 'none' ? session.attentionState : session.attentionReason;
  }

  formatFileStatus(): string {
    if (!this.selectedFilePath) {
      return '';
    }
    return `${this.selectedFileSizeBytes} bytes${this.selectedFileTruncated ? ' · truncated' : ''}`;
  }

  private async loadSnapshot(sessionId: string): Promise<void> {
    try {
      const payload = await this.fetchJson<SessionSnapshotResponse>(`/sessions/${encodeURIComponent(sessionId)}/snapshot`);
      this.selectedSnapshot = payload;
      const matchingTab = this.openTabs.find((tab) => tab.sessionId === sessionId);
      if (matchingTab && payload.recentTerminalTail && !matchingTab.terminalOutput) {
        matchingTab.terminalOutput = payload.recentTerminalTail;
        if (this.activeTabSessionId === sessionId) {
          this.refreshTerminal();
        }
      }
    } catch (error) {
      this.log(`snapshot load failed for ${sessionId}: ${String(error)}`);
    }
  }

  private initializeTerminal(): void {
    if (!this.terminalHost?.nativeElement || this.terminal) {
      return;
    }

    this.terminal = new Terminal({
      convertEol: false,
      cursorBlink: true,
      fontFamily: 'Iosevka Comfy, SF Mono, Menlo, monospace',
      fontSize: 14,
      scrollback: 5000,
      theme: {
        background: '#231b16',
        foreground: '#f8e9d5',
        cursor: '#f0b061',
        selectionBackground: 'rgba(240, 176, 97, 0.28)',
      },
    });
    this.fitAddon = new FitAddon();
    this.terminal.loadAddon(this.fitAddon);
    this.terminal.open(this.terminalHost.nativeElement);
    this.fitTerminal();
    this.refreshTerminal();

    this.terminal.onData((data) => {
      const tab = this.activeTab;
      if (!tab || tab.connectionState !== 'connected' || tab.controllerState !== 'remote') {
        return;
      }
      this.sendSessionCommand(tab.sessionId, { type: 'terminal.input', data });
    });

    this.resizeObserver = new ResizeObserver(() => {
      this.fitTerminal();
      if (this.activeTab?.connectionState === 'connected') {
        this.sendResizeForTab(this.activeTab.sessionId);
      }
    });
    this.resizeObserver.observe(this.terminalHost.nativeElement);
  }

  private fitTerminal(): void {
    this.fitAddon?.fit();
  }

  private refreshTerminal(): void {
    if (!this.terminal) {
      return;
    }
    const content = this.activeTab?.terminalOutput || this.selectedSnapshot?.recentTerminalTail || '';
    this.terminal.reset();
    if (content) {
      this.terminal.write(content);
    }
  }

  private sendResizeForTab(sessionId: string): void {
    if (!this.terminal) {
      return;
    }
    this.sendSessionCommand(sessionId, {
      type: 'terminal.resize',
      cols: this.terminal.cols,
      rows: this.terminal.rows,
    });
  }

  private pollPairingClaim(pairingId: string, code: string): void {
    if (this.pairingPollTimer !== null) {
      clearTimeout(this.pairingPollTimer);
    }
    this.pairingPollTimer = setTimeout(async () => {
      try {
        const payload = await this.fetchJson<PairingClaimResponse>('/pairing/claim', {
          method: 'POST',
          body: JSON.stringify({ pairingId, code }),
        });
        if (payload.token) {
          this.token = payload.token;
          this.persistSettings();
          this.pairingText = JSON.stringify({ pairingId, code, status: 'approved', token: payload.token }, null, 2);
          this.log(`pairing approved: token received for ${pairingId}`);
          this.connectOverviewSocket();
          return;
        }
      } catch {
        this.pairingPollTimer = setTimeout(() => this.pollPairingClaim(pairingId, code), 2000);
        return;
      }
      this.pairingPollTimer = setTimeout(() => this.pollPairingClaim(pairingId, code), 2000);
    }, 2000);
  }

  private connectOverviewSocket(): void {
    this.disconnectOverviewSocket();
    if (!this.token.trim()) {
      return;
    }

    const socket = new WebSocket(this.overviewWebsocketUrl());
    this.overviewSocket = socket;
    this.overviewConnection = 'connecting';

    socket.addEventListener('open', () => {
      this.overviewConnection = 'connected';
      this.log('overview websocket open');
    });

    socket.addEventListener('message', (event) => {
      try {
        const payload = JSON.parse(String(event.data)) as { type?: string; sessions?: RawSessionSummary[] };
        if (payload.type === 'sessions.snapshot' && Array.isArray(payload.sessions)) {
          this.applySessions(payload.sessions);
        }
      } catch (error) {
        this.log(`overview parse error: ${String(error)}`);
      }
    });

    socket.addEventListener('close', () => {
      this.overviewSocket = null;
      this.overviewConnection = 'disconnected';
      if (this.token.trim()) {
        this.overviewReconnectTimer = setTimeout(() => this.connectOverviewSocket(), 3000);
      }
    });

    socket.addEventListener('error', () => {
      this.log('overview websocket error');
    });
  }

  private disconnectOverviewSocket(): void {
    if (this.overviewReconnectTimer !== null) {
      clearTimeout(this.overviewReconnectTimer);
      this.overviewReconnectTimer = null;
    }
    if (this.overviewSocket) {
      this.overviewSocket.close();
      this.overviewSocket = null;
    }
    this.overviewConnection = 'disconnected';
  }

  private handleSessionMessage(sessionId: string, rawMessage: string): void {
    const tab = this.openTabs.find((item) => item.sessionId === sessionId);
    if (!tab) {
      return;
    }

    try {
      const payload = JSON.parse(rawMessage) as Record<string, unknown>;
      const type = String(payload['type'] ?? '');
      if (type === 'terminal.output') {
        const output = this.decodeBase64(String(payload['dataBase64'] ?? ''));
        tab.terminalOutput += output;
        tab.lastEvent = 'Terminal output received.';
        if (this.activeTabSessionId === sessionId) {
          this.terminal?.write(output);
        }
      } else if (type === 'session.updated') {
        const updated = payload as unknown as RawSessionSummary;
        this.mergeSessionUpdate(updated);
        tab.controllerState = this.toControllerState(updated.controllerKind);
        tab.lastEvent = 'Session updated.';
      } else if (type === 'session.exited') {
        tab.lastEvent = 'Session exited.';
        tab.connectionState = 'disconnected';
      } else {
        tab.lastEvent = type || 'Unknown event.';
      }
      if (this.selectedSessionId === sessionId) {
        void this.loadSnapshot(sessionId);
      }
    } catch {
      tab.terminalOutput += rawMessage;
      tab.lastEvent = 'Raw frame received.';
    }
  }

  private sendSessionCommand(sessionId: string, payload: object): void {
    const socket = this.sessionSockets.get(sessionId);
    if (!socket || socket.readyState !== WebSocket.OPEN) {
      this.log(`session websocket not open for ${sessionId}`);
      return;
    }
    socket.send(JSON.stringify(payload));
  }

  private disconnectTab(sessionId: string): void {
    const socket = this.sessionSockets.get(sessionId);
    if (socket) {
      socket.close();
      this.sessionSockets.delete(sessionId);
    }
    const tab = this.openTabs.find((item) => item.sessionId === sessionId);
    if (tab) {
      tab.connectionState = 'disconnected';
      tab.controllerState = 'observer';
      tab.lastEvent = 'Disconnected.';
      if (this.activeTabSessionId === sessionId) {
        this.refreshTerminal();
      }
    }
  }

  private applySessions(payload: RawSessionSummary[]): void {
    this.sessions = payload.map((session) => this.mapSession(session));
    this.sessions = this.sortedSessions;

    if (!this.selectedSessionId && this.sessions.length > 0) {
      this.selectedSessionId = this.sessions[0].sessionId;
    }

    this.selectedSession = this.sessions.find((session) => session.sessionId === this.selectedSessionId) ?? null;
    if (this.selectedSession && !this.selectedSnapshot) {
      void this.loadSnapshot(this.selectedSession.sessionId);
    }

    for (const tab of this.openTabs) {
      const session = this.sessions.find((item) => item.sessionId === tab.sessionId);
      if (session) {
        tab.title = session.title;
        if (this.selectedSessionId === tab.sessionId) {
          tab.controllerState = this.toControllerState(session.controllerKind);
        }
      }
    }
    this.persistSettings();
  }

  private mergeSessionUpdate(updated: RawSessionSummary): void {
    const mapped = this.mapSession(updated);
    const index = this.sessions.findIndex((session) => session.sessionId === mapped.sessionId);
    if (index >= 0) {
      this.sessions[index] = mapped;
    } else {
      this.sessions.push(mapped);
    }
    this.sessions = this.sortedSessions;
    if (this.selectedSessionId === mapped.sessionId) {
      this.selectedSession = mapped;
    }
  }

  private mapSession(session: RawSessionSummary): SessionSummaryView {
    return {
      sessionId: session.sessionId,
      title: session.title || '(untitled)',
      provider: session.provider || 'codex',
      lifecycleStatus: this.toLifecycle(session.status),
      inventoryState: session.inventoryState ?? 'ended',
      attentionState: session.attentionState ?? 'none',
      attentionReason: session.attentionReason ?? 'none',
      controllerKind: session.controllerKind ?? 'none',
      attachedClientCount: session.attachedClientCount ?? 0,
      conversationLabel: session.conversationId || 'fresh',
      lastActivityLabel: this.formatRelative(session.lastActivityAtUnixMs),
      lastOutputLabel: this.formatRelative(session.lastOutputAtUnixMs),
      gitLabel: session.gitDirty ? `dirty (${session.gitBranch || 'unknown'})` : 'clean',
      recentFileChangeCount: session.recentFileChangeCount ?? 0,
    };
  }

  private toLifecycle(status: string): SessionSummaryView['lifecycleStatus'] {
    if (status === 'AwaitingInput' || status === 'Exited' || status === 'Error' || status === 'Starting') {
      return status;
    }
    return 'Running';
  }

  private toControllerState(kind: unknown): ControllerState {
    if (kind === 'host' || kind === 'remote') {
      return kind;
    }
    return 'observer';
  }

  private restoreOpenTabs(): void {
    const storedTabs = this.loadStored('openTabs', '[]');
    try {
      const parsed = JSON.parse(storedTabs) as Array<{ sessionId: string; title: string }>;
      this.openTabs = parsed.map((tab) => ({
        sessionId: tab.sessionId,
        title: tab.title,
        connectionState: 'disconnected',
        controllerState: 'observer',
        terminalOutput: '',
        lastEvent: 'Restored tab.',
      }));
      this.activeTabSessionId = this.loadStored('activeTabSessionId', this.openTabs[0]?.sessionId ?? '');
    } catch {
      this.openTabs = [];
      this.activeTabSessionId = '';
    }
  }

  persistSettings(): void {
    if (typeof window === 'undefined') {
      return;
    }
    localStorage.setItem(
      'vibe-angular-remote-client',
      JSON.stringify({
        host: this.host,
        port: this.port,
        token: this.token,
        provider: this.provider,
        title: this.title,
        workspaceRoot: this.workspaceRoot,
        conversationId: this.conversationId,
        command: this.command,
        sortMode: this.sortMode,
        selectedSessionId: this.selectedSessionId,
        activeTabSessionId: this.activeTabSessionId,
        openTabs: this.openTabs.map((tab) => ({ sessionId: tab.sessionId, title: tab.title })),
      }),
    );
  }

  private loadStored(key: string, fallback: string): string {
    if (typeof window === 'undefined') {
      return fallback;
    }
    const raw = localStorage.getItem('vibe-angular-remote-client');
    if (!raw) {
      return fallback;
    }
    try {
      const parsed = JSON.parse(raw) as Record<string, string>;
      return parsed[key] ?? fallback;
    } catch {
      return fallback;
    }
  }

  private async fetchJson<T>(path: string, init?: RequestInit): Promise<T> {
    const headers = new Headers(init?.headers || {});
    if (this.token.trim()) {
      headers.set('authorization', `Bearer ${this.token.trim()}`);
    }
    if (init?.body && !headers.has('content-type')) {
      headers.set('content-type', 'application/json');
    }

    const response = await fetch(`${this.baseUrl()}${path}`, { ...init, headers });
    const text = await response.text();
    const payload = text ? JSON.parse(text) : null;
    if (!response.ok) {
      throw new Error(typeof payload === 'string' ? payload : JSON.stringify(payload));
    }
    return payload as T;
  }

  private baseUrl(): string {
    return `${window.location.protocol === 'https:' ? 'https:' : 'http:'}//${this.host.trim()}:${this.port.trim()}`;
  }

  private overviewWebsocketUrl(): string {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    return `${protocol}//${this.host.trim()}:${this.port.trim()}/ws/overview?access_token=${encodeURIComponent(this.token.trim())}`;
  }

  private sessionWebsocketUrl(sessionId: string): string {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    return `${protocol}//${this.host.trim()}:${this.port.trim()}/ws/sessions/${sessionId}?access_token=${encodeURIComponent(this.token.trim())}`;
  }

  private parseCommand(command: string): string[] {
    return command.trim() ? command.trim().split(/\s+/).filter(Boolean) : [];
  }

  private decodeBase64(encoded: string): string {
    const binary = atob(encoded || '');
    const bytes = Uint8Array.from(binary, (char) => char.charCodeAt(0));
    return new TextDecoder().decode(bytes);
  }

  private compareRelativeLabel(left: string, right: string): number {
    const parse = (value: string): number => {
      if (value === 'just now') {
        return 0;
      }
      const match = value.match(/^(\d+)([smhd]) ago$/);
      if (!match) {
        return Number.MAX_SAFE_INTEGER;
      }
      const amount = Number(match[1]);
      const unit = match[2];
      switch (unit) {
        case 's':
          return amount;
        case 'm':
          return amount * 60;
        case 'h':
          return amount * 3600;
        case 'd':
          return amount * 86400;
        default:
          return Number.MAX_SAFE_INTEGER;
      }
    };
    return parse(left) - parse(right);
  }

  private formatRelative(unixMs?: number): string {
    if (!unixMs || !Number.isFinite(unixMs)) {
      return 'n/a';
    }
    const deltaMs = Date.now() - unixMs;
    if (deltaMs < 1000) {
      return 'just now';
    }
    const seconds = Math.floor(deltaMs / 1000);
    if (seconds < 60) {
      return `${seconds}s ago`;
    }
    const minutes = Math.floor(seconds / 60);
    if (minutes < 60) {
      return `${minutes}m ago`;
    }
    const hours = Math.floor(minutes / 60);
    if (hours < 24) {
      return `${hours}h ago`;
    }
    return `${Math.floor(hours / 24)}d ago`;
  }

  private defaultHost(): string {
    return this.loadStored('host', window.location.hostname || '127.0.0.1');
  }

  private defaultPort(): string {
    const stored = this.loadStored('port', '');
    if (stored) {
      return stored;
    }

    const currentPort = window.location.port;
    if (currentPort === '18086') {
      return '18086';
    }
    if (currentPort === '4201' || currentPort === '4200') {
      return '18086';
    }
    return currentPort || (window.location.protocol === 'https:' ? '443' : '80');
  }

  private log(message: string): void {
    const stamp = new Date().toLocaleTimeString();
    this.events = [...this.events.slice(-159), `[${stamp}] ${message}`];
  }
}
