import { Component, OnDestroy, OnInit } from '@angular/core';
import { FormsModule } from '@angular/forms';
import { SessionSummaryView, TrustedDeviceView } from '../../../session-model/src/public-api';
import { attentionTone, inventoryTone } from '../../../shared-ui/src/public-api';

interface HostInfoResponse {
  displayName: string;
  adminHost: string;
  adminPort: number;
  remoteHost: string;
  remotePort: number;
  providerCommands?: Record<string, string[]>;
  tls?: {
    enabled?: boolean;
    mode?: string;
  };
}

interface PendingPairingView {
  pairingId: string;
  deviceName: string;
  deviceType: string;
  code: string;
  requestedAtUnixMs?: number;
  requestedLabel: string;
}

interface RawSessionSummary {
  sessionId: string;
  title: string;
  provider: string;
  status: SessionSummaryView['lifecycleStatus'];
  inventoryState?: SessionSummaryView['inventoryState'];
  attentionState?: SessionSummaryView['attentionState'];
  attentionReason?: SessionSummaryView['attentionReason'];
  controllerKind?: SessionSummaryView['controllerKind'];
  attachedClientCount?: number;
  conversationId?: string;
  lastActivityAtUnixMs?: number;
  lastOutputAtUnixMs?: number;
  gitDirty?: boolean;
  gitBranch?: string;
  recentFileChangeCount?: number;
}

interface AttachedClientView {
  clientId: string;
  sessionId: string;
  sessionTitle: string;
  sessionStatus: string;
  clientAddress: string;
  claimedKind: string;
  isLocal: boolean;
  hasControl: boolean;
  connectedAtUnixMs?: number;
}

interface HostLogsResponse {
  path: string;
  source: string;
  available: boolean;
  message?: string;
  entries: string[];
}

interface HostConfigDraft {
  displayName: string;
  adminHost: string;
  adminPort: number;
  remoteHost: string;
  remotePort: number;
  codexCommand: string;
  claudeCommand: string;
}

interface CreateSessionDraft {
  provider: 'codex' | 'claude';
  title: string;
  workspaceRoot: string;
  conversationId: string;
  command: string;
}

type HostViewTab = 'setup' | 'authentication' | 'sessions' | 'activity';

@Component({
  selector: 'app-root',
  standalone: true,
  imports: [FormsModule],
  templateUrl: './app.html',
  styleUrl: './app.css',
})
export class App implements OnInit, OnDestroy {
  readonly apiBase = this.detectApiBase();
  activeViewTab: HostViewTab = 'setup';

  hostInfo: HostInfoResponse | null = null;
  hostConfig: HostConfigDraft = {
    displayName: '',
    adminHost: '127.0.0.1',
    adminPort: 18085,
    remoteHost: '0.0.0.0',
    remotePort: 18086,
    codexCommand: '',
    claudeCommand: '',
  };
  createSessionDraft: CreateSessionDraft = {
    provider: 'codex',
    title: 'host-session',
    workspaceRoot: '',
    conversationId: '',
    command: '',
  };

  sessions: SessionSummaryView[] = [];
  trustedDevices: TrustedDeviceView[] = [];
  pendingPairings: PendingPairingView[] = [];
  clients: AttachedClientView[] = [];
  logs: string[] = [];
  hostLogs: string[] = [];
  hostLogPath = '';
  hostLogSource = '';
  hostLogsAvailable = false;
  hostLogsMessage = 'No daemon logs yet.';

  loading = false;
  savingConfig = false;
  creatingSession = false;

  protected readonly inventoryTone = inventoryTone;
  protected readonly attentionTone = attentionTone;

  private refreshTimer: ReturnType<typeof setInterval> | null = null;
  private lastPendingPairingCount = 0;

  async ngOnInit(): Promise<void> {
    this.createSessionDraft.workspaceRoot = this.defaultWorkspaceRoot();
    await this.refreshAll();
    this.refreshTimer = setInterval(() => {
      void this.refreshOperationalState();
    }, 5000);
  }

  ngOnDestroy(): void {
    if (this.refreshTimer !== null) {
      clearInterval(this.refreshTimer);
    }
  }

  async refreshAll(): Promise<void> {
    this.loading = true;
    try {
      const [hostInfo, pendingPairings, trustedDevices, sessions, clients, hostLogs] = await Promise.all([
        this.fetchJson<HostInfoResponse>('/host/info'),
        this.fetchJson<Array<Omit<PendingPairingView, 'requestedLabel'>>>('/pairing/pending'),
        this.fetchJson<Array<{ deviceId: string; deviceName: string; deviceType: string; approvedAtUnixMs?: number }>>(
          '/host/trusted-devices',
        ),
        this.fetchJson<RawSessionSummary[]>('/host/sessions'),
        this.fetchJson<AttachedClientView[]>('/host/clients'),
        this.fetchJson<HostLogsResponse>('/host/logs'),
      ]);

      this.applyHostInfo(hostInfo);
      this.pendingPairings = pendingPairings.map((pairing) => ({
        ...pairing,
        requestedLabel: this.formatRelative(pairing.requestedAtUnixMs),
      }));
      this.maybeFocusAuthenticationTab(this.pendingPairings.length);
      this.trustedDevices = trustedDevices.map((device) => ({
        deviceId: device.deviceId,
        deviceName: device.deviceName,
        deviceType: device.deviceType,
        tokenAgeLabel: this.formatRelative(device.approvedAtUnixMs),
        lastSeenLabel: 'n/a',
      }));
      this.sessions = sessions.map((session) => this.mapSession(session));
      this.clients = clients;
      this.applyHostLogs(hostLogs);
      this.sortOperationalData();
    } catch (error) {
      this.log(`refresh failed: ${String(error)}`);
    } finally {
      this.loading = false;
    }
  }

  async refreshOperationalState(): Promise<void> {
    try {
      const [pendingPairings, trustedDevices, sessions, clients, hostLogs] = await Promise.all([
        this.fetchJson<Array<Omit<PendingPairingView, 'requestedLabel'>>>('/pairing/pending'),
        this.fetchJson<Array<{ deviceId: string; deviceName: string; deviceType: string; approvedAtUnixMs?: number }>>(
          '/host/trusted-devices',
        ),
        this.fetchJson<RawSessionSummary[]>('/host/sessions'),
        this.fetchJson<AttachedClientView[]>('/host/clients'),
        this.fetchJson<HostLogsResponse>('/host/logs'),
      ]);

      this.pendingPairings = pendingPairings.map((pairing) => ({
        ...pairing,
        requestedLabel: this.formatRelative(pairing.requestedAtUnixMs),
      }));
      this.maybeFocusAuthenticationTab(this.pendingPairings.length);
      this.trustedDevices = trustedDevices.map((device) => ({
        deviceId: device.deviceId,
        deviceName: device.deviceName,
        deviceType: device.deviceType,
        tokenAgeLabel: this.formatRelative(device.approvedAtUnixMs),
        lastSeenLabel: 'n/a',
      }));
      this.sessions = sessions.map((session) => this.mapSession(session));
      this.clients = clients;
      this.applyHostLogs(hostLogs);
      this.sortOperationalData();
    } catch (error) {
      this.log(`background refresh failed: ${String(error)}`);
    }
  }

  async approvePairing(pairing: PendingPairingView): Promise<void> {
    try {
      const payload = await this.fetchJson<{ deviceId?: string }>('/pairing/approve', {
        method: 'POST',
        headers: { 'content-type': 'application/json' },
        body: JSON.stringify({ pairingId: pairing.pairingId, code: pairing.code }),
      });
      this.log(`approved pairing ${pairing.pairingId}${payload.deviceId ? ` -> ${payload.deviceId}` : ''}`);
      await this.refreshOperationalState();
    } catch (error) {
      this.log(`approve failed for ${pairing.pairingId}: ${String(error)}`);
    }
  }

  async saveConfig(): Promise<void> {
    this.savingConfig = true;
    try {
      const payload = {
        displayName: this.hostConfig.displayName.trim(),
        adminHost: this.hostConfig.adminHost.trim(),
        adminPort: Number(this.hostConfig.adminPort),
        remoteHost: this.hostConfig.remoteHost.trim(),
        remotePort: Number(this.hostConfig.remotePort),
        providerCommands: this.buildProviderCommands(),
      };
      const hostInfo = await this.fetchJson<HostInfoResponse>('/host/config', {
        method: 'POST',
        headers: { 'content-type': 'application/json' },
        body: JSON.stringify(payload),
      });
      this.applyHostInfo(hostInfo);
      this.log('saved host config');
    } catch (error) {
      this.log(`save host config failed: ${String(error)}`);
    } finally {
      this.savingConfig = false;
    }
  }

  async createSession(): Promise<void> {
    this.creatingSession = true;
    try {
      const payload = {
        provider: this.createSessionDraft.provider,
        workspaceRoot: this.createSessionDraft.workspaceRoot.trim(),
        title: this.createSessionDraft.title.trim() || 'host-session',
        ...(this.createSessionDraft.conversationId.trim()
          ? { conversationId: this.createSessionDraft.conversationId.trim() }
          : {}),
        ...(this.parseCommand(this.createSessionDraft.command).length > 0
          ? { command: this.parseCommand(this.createSessionDraft.command) }
          : {}),
      };
      const created = await this.fetchJson<{ sessionId: string; title?: string }>('/host/sessions', {
        method: 'POST',
        headers: { 'content-type': 'application/json' },
        body: JSON.stringify(payload),
      });
      this.log(`created session ${created.sessionId}`);
      this.createSessionDraft.title = 'host-session';
      this.createSessionDraft.conversationId = '';
      this.createSessionDraft.command = '';
      await this.refreshOperationalState();
    } catch (error) {
      this.log(`create session failed: ${String(error)}`);
    } finally {
      this.creatingSession = false;
    }
  }

  async clearEndedArchived(): Promise<void> {
    try {
      const payload = await this.fetchJson<{ removedCount?: number }>('/host/sessions/clear-inactive', {
        method: 'POST',
      });
      this.log(`cleared ${payload.removedCount ?? 0} ended/archive record(s)`);
      await this.refreshOperationalState();
    } catch (error) {
      this.log(`clear ended/archive failed: ${String(error)}`);
    }
  }

  async stopSession(sessionId: string): Promise<void> {
    try {
      await this.fetchJson(`/host/sessions/${encodeURIComponent(sessionId)}/stop`, { method: 'POST' });
      this.log(`stopped session ${sessionId}`);
      await this.refreshOperationalState();
    } catch (error) {
      this.log(`stop failed for ${sessionId}: ${String(error)}`);
    }
  }

  async revokeTrustedDevice(deviceId: string, action: 'expire' | 'remove'): Promise<void> {
    try {
      await this.fetchJson(`/host/trusted-devices/${encodeURIComponent(deviceId)}/${action}`, {
        method: 'POST',
      });
      this.log(`${action === 'expire' ? 'expired' : 'removed'} trusted device ${deviceId}`);
      await this.refreshOperationalState();
    } catch (error) {
      this.log(`${action} failed for ${deviceId}: ${String(error)}`);
    }
  }

  async disconnectClient(clientId: string): Promise<void> {
    try {
      await this.fetchJson(`/host/clients/${encodeURIComponent(clientId)}/disconnect`, { method: 'POST' });
      this.log(`disconnected client ${clientId}`);
      await this.refreshOperationalState();
    } catch (error) {
      this.log(`disconnect failed for ${clientId}: ${String(error)}`);
    }
  }

  async copySessionId(sessionId: string): Promise<void> {
    await this.copyText(sessionId, `copied session id ${sessionId}`);
  }

  async copyCliCommand(sessionId: string): Promise<void> {
    const command =
      `./build/sentrits session-attach --host ${this.hostConfig.adminHost} ` +
      `--port ${this.hostConfig.adminPort} ${sessionId}`;
    await this.copyText(command, `copied attach command for ${sessionId}`);
  }

  async copyHostInfo(): Promise<void> {
    const summary = JSON.stringify(
      {
        displayName: this.hostConfig.displayName,
        admin: `${this.hostConfig.adminHost}:${this.hostConfig.adminPort}`,
        remote: `${this.hostConfig.remoteHost}:${this.hostConfig.remotePort}`,
      },
      null,
      2,
    );
    await this.copyText(summary, 'copied host info');
  }

  async copyHostLogPath(): Promise<void> {
    if (!this.hostLogPath) {
      this.log('host log path unavailable');
      return;
    }
    await this.copyText(this.hostLogPath, 'copied daemon log path');
  }

  downloadRemoteCertificate(): void {
    const target = `${this.apiBase}/host/tls/certificate`;
    window.open(target, '_blank', 'noopener,noreferrer');
  }

  sessionClients(sessionId: string): AttachedClientView[] {
    return this.clients
      .filter((client) => client.sessionId === sessionId)
      .sort((left, right) => Number(right.hasControl) - Number(left.hasControl));
  }

  clientRole(client: AttachedClientView): string {
    if (client.hasControl) {
      return client.claimedKind === 'host' ? 'Host controller' : 'Remote controller';
    }
    if (client.claimedKind === 'host') {
      return 'Host observer';
    }
    if (client.claimedKind === 'remote') {
      return 'Remote observer';
    }
    return 'Observer';
  }

  canStop(session: SessionSummaryView): boolean {
    return session.lifecycleStatus !== 'Exited' && session.lifecycleStatus !== 'Error';
  }

  selectViewTab(tab: HostViewTab): void {
    this.activeViewTab = tab;
  }

  private detectApiBase(): string {
    if (typeof window === 'undefined') {
      return 'http://127.0.0.1:18085';
    }

    const port = window.location.port;
    const hostname = window.location.hostname;
    const isAngularDevServer = port === '4200' || port === '4300';
    if (isAngularDevServer) {
      return 'http://127.0.0.1:18085';
    }

    if ((hostname === 'localhost' || hostname === '127.0.0.1') && port === '') {
      return 'http://127.0.0.1:18085';
    }

    return '';
  }

  private defaultWorkspaceRoot(): string {
    return '/Users/shubow/dev/Sentrits-Core';
  }

  private parseCommand(commandText: string): string[] {
    const trimmed = commandText.trim();
    return trimmed ? trimmed.split(/\s+/).filter(Boolean) : [];
  }

  private buildProviderCommands(): Record<string, string[]> {
    const commands: Record<string, string[]> = {};
    const codex = this.parseCommand(this.hostConfig.codexCommand);
    const claude = this.parseCommand(this.hostConfig.claudeCommand);
    if (codex.length > 0) {
      commands['codex'] = codex;
    }
    if (claude.length > 0) {
      commands['claude'] = claude;
    }
    return commands;
  }

  private applyHostInfo(hostInfo: HostInfoResponse): void {
    this.hostInfo = hostInfo;
    this.hostConfig = {
      displayName: hostInfo.displayName ?? '',
      adminHost: hostInfo.adminHost ?? '127.0.0.1',
      adminPort: hostInfo.adminPort ?? 18085,
      remoteHost: hostInfo.remoteHost ?? '0.0.0.0',
      remotePort: hostInfo.remotePort ?? 18086,
      codexCommand: (hostInfo.providerCommands?.['codex'] ?? []).join(' '),
      claudeCommand: (hostInfo.providerCommands?.['claude'] ?? []).join(' '),
    };
  }

  private applyHostLogs(hostLogs: HostLogsResponse): void {
    this.hostLogs = hostLogs.entries ?? [];
    this.hostLogPath = hostLogs.path ?? '';
    this.hostLogSource = hostLogs.source ?? '';
    this.hostLogsAvailable = Boolean(hostLogs.available);
    this.hostLogsMessage = hostLogs.message ?? (this.hostLogsAvailable ? '' : 'No daemon logs yet.');
  }

  private mapSession(session: RawSessionSummary): SessionSummaryView {
    return {
      sessionId: session.sessionId,
      title: session.title || '(untitled)',
      provider: session.provider || 'codex',
      lifecycleStatus: session.status,
      inventoryState: session.inventoryState ?? 'ended',
      attentionState: session.attentionState ?? 'none',
      attentionReason: session.attentionReason ?? 'none',
      controllerKind: session.controllerKind ?? 'none',
      attachedClientCount: session.attachedClientCount ?? 0,
      conversationLabel: session.conversationId ?? 'fresh',
      lastActivityLabel: this.formatRelative(session.lastActivityAtUnixMs),
      lastOutputLabel: this.formatRelative(session.lastOutputAtUnixMs),
      gitLabel: session.gitDirty ? `dirty (${session.gitBranch || 'unknown'})` : 'clean',
      recentFileChangeCount: session.recentFileChangeCount ?? 0,
    };
  }

  private sortOperationalData(): void {
    const sessionRank = (session: SessionSummaryView): number => {
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

    this.sessions.sort((left, right) => {
      const rank = sessionRank(left) - sessionRank(right);
      if (rank !== 0) {
        return rank;
      }
      return left.sessionId.localeCompare(right.sessionId, undefined, { numeric: true });
    });

    this.pendingPairings.sort((left, right) => left.pairingId.localeCompare(right.pairingId));
    this.trustedDevices.sort((left, right) => left.deviceName.localeCompare(right.deviceName));
    this.clients.sort((left, right) => left.sessionId.localeCompare(right.sessionId, undefined, { numeric: true }));
  }

  private maybeFocusAuthenticationTab(nextPendingCount: number): void {
    if (nextPendingCount > 0 && nextPendingCount > this.lastPendingPairingCount) {
      this.activeViewTab = 'authentication';
    }
    this.lastPendingPairingCount = nextPendingCount;
  }

  private formatRelative(unixMs?: number): string {
    if (!unixMs || !Number.isFinite(unixMs)) {
      return 'n/a';
    }
    const deltaMs = Date.now() - unixMs;
    if (deltaMs < 0) {
      return 'just now';
    }

    const seconds = Math.floor(deltaMs / 1000);
    if (seconds < 10) {
      return 'just now';
    }
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

    const days = Math.floor(hours / 24);
    return `${days}d ago`;
  }

  private async copyText(text: string, message: string): Promise<void> {
    try {
      await navigator.clipboard.writeText(text);
      this.log(message);
    } catch (error) {
      this.log(`copy failed: ${String(error)}`);
    }
  }

  private async fetchJson<T>(path: string, init?: RequestInit): Promise<T> {
    const response = await fetch(`${this.apiBase}${path}`, init);
    const text = await response.text();
    const payload = text ? JSON.parse(text) : null;
    if (!response.ok) {
      throw new Error(typeof payload === 'string' ? payload : JSON.stringify(payload));
    }
    return payload as T;
  }

  private log(message: string): void {
    const stamp = new Date().toLocaleTimeString();
    this.logs = [...this.logs.slice(-119), `[${stamp}] ${message}`];
  }
}
