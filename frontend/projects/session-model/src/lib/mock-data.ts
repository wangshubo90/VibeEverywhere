import {
  OpenSessionTabView,
  PendingPairingView,
  RemoteSessionSortMode,
  SessionSummaryView,
  TrustedDeviceView,
} from './session-model';

export const HOST_SAMPLE_SESSIONS: SessionSummaryView[] = [
  {
    sessionId: 's_1',
    title: 'codex-runtime',
    provider: 'codex',
    lifecycleStatus: 'Running',
    inventoryState: 'live',
    attentionState: 'action_required',
    attentionReason: 'awaiting_input',
    controllerKind: 'host',
    attachedClientCount: 0,
    conversationLabel: 'fresh',
    lastActivityLabel: '12s ago',
    lastOutputLabel: '14s ago',
    gitLabel: 'dirty (main)',
    recentFileChangeCount: 2,
  },
  {
    sessionId: 's_2',
    title: 'cleanup-pass',
    provider: 'claude',
    lifecycleStatus: 'Exited',
    inventoryState: 'ended',
    attentionState: 'info',
    attentionReason: 'session_exited_cleanly',
    controllerKind: 'none',
    attachedClientCount: 0,
    conversationLabel: 'conv_a1f3',
    lastActivityLabel: '3m ago',
    lastOutputLabel: '3m ago',
    gitLabel: 'clean',
    recentFileChangeCount: 0,
  },
];

export const REMOTE_SAMPLE_SESSIONS: SessionSummaryView[] = [
  {
    sessionId: 's_1',
    title: 'codex-runtime',
    provider: 'codex',
    lifecycleStatus: 'Running',
    inventoryState: 'live',
    attentionState: 'action_required',
    attentionReason: 'awaiting_input',
    controllerKind: 'host',
    attachedClientCount: 0,
    conversationLabel: 'fresh',
    lastActivityLabel: '12s ago',
    lastOutputLabel: '14s ago',
    gitLabel: 'dirty (main)',
    recentFileChangeCount: 2,
  },
  {
    sessionId: 's_7',
    title: 'refactor-queue',
    provider: 'codex',
    lifecycleStatus: 'Running',
    inventoryState: 'live',
    attentionState: 'info',
    attentionReason: 'workspace_changed',
    controllerKind: 'remote',
    attachedClientCount: 1,
    conversationLabel: 'conv_7c9',
    lastActivityLabel: '1m ago',
    lastOutputLabel: '1m ago',
    gitLabel: 'dirty (feature/ui)',
    recentFileChangeCount: 4,
  },
];

export const SAMPLE_TRUSTED_DEVICES: TrustedDeviceView[] = [
  {
    deviceId: 'dev_101',
    deviceName: 'iPhone smoke',
    deviceType: 'mobile',
    tokenAgeLabel: '2d',
    lastSeenLabel: '5m ago',
  },
  {
    deviceId: 'dev_202',
    deviceName: 'MacBook Safari',
    deviceType: 'browser',
    tokenAgeLabel: '8h',
    lastSeenLabel: 'just now',
  },
];

export const SAMPLE_PENDING_PAIRINGS: PendingPairingView[] = [
  {
    pairingId: 'p_72f0',
    deviceName: 'New iPad',
    deviceType: 'browser',
    code: '472 918',
    requestedLabel: '10s ago',
  },
];

export const SAMPLE_OPEN_TABS: OpenSessionTabView[] = [
  {
    sessionId: 's_1',
    title: 'codex-runtime',
    connected: true,
    hasControl: false,
  },
  {
    sessionId: 's_7',
    title: 'refactor-queue',
    connected: false,
    hasControl: false,
  },
];

export const REMOTE_SORT_MODES: Array<{ value: RemoteSessionSortMode; label: string }> = [
  { value: 'attention', label: 'Attention' },
  { value: 'recent-activity', label: 'Recent activity' },
  { value: 'recent-output', label: 'Recent output' },
  { value: 'created', label: 'Created time' },
  { value: 'title', label: 'Title' },
  { value: 'provider', label: 'Provider' },
];
