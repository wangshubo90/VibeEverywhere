export type SessionLifecycleStatus =
  | 'Running'
  | 'AwaitingInput'
  | 'Exited'
  | 'Error'
  | 'Starting';

export type SessionInventoryState = 'live' | 'ended' | 'archived';

export type SessionAttentionState = 'none' | 'info' | 'action_required' | 'intervention';

export type SessionAttentionReason =
  | 'none'
  | 'awaiting_input'
  | 'session_error'
  | 'workspace_changed'
  | 'git_state_changed'
  | 'controller_changed'
  | 'session_exited_cleanly';

export type SessionControllerKind = 'host' | 'remote' | 'none';

export interface SessionSummaryView {
  sessionId: string;
  title: string;
  provider: string;
  lifecycleStatus: SessionLifecycleStatus;
  inventoryState: SessionInventoryState;
  attentionState: SessionAttentionState;
  attentionReason: SessionAttentionReason;
  controllerKind: SessionControllerKind;
  attachedClientCount: number;
  conversationLabel: string;
  lastActivityLabel: string;
  lastOutputLabel: string;
  gitLabel: string;
  recentFileChangeCount: number;
}

export interface TrustedDeviceView {
  deviceId: string;
  deviceName: string;
  deviceType: string;
  tokenAgeLabel: string;
  lastSeenLabel: string;
}

export interface PendingPairingView {
  pairingId: string;
  deviceName: string;
  deviceType: string;
  code: string;
  requestedLabel: string;
}

export interface OpenSessionTabView {
  sessionId: string;
  title: string;
  connected: boolean;
  hasControl: boolean;
}

export type RemoteSessionSortMode =
  | 'attention'
  | 'recent-activity'
  | 'recent-output'
  | 'created'
  | 'title'
  | 'provider';
