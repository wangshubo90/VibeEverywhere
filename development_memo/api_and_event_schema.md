# API and Event Schema Draft

This document translates the initiative API into a more implementation-ready contract.

## REST Endpoints

All non-health endpoints should be designed to support bearer-token authorization in the MVP.

Internal note:

- the public remote API remains HTTP plus WebSocket on the remote listener
- host-local `session start --attach` and `session attach` now use an internal privileged controller stream for low-latency control
- that local controller stream is daemon-private and is not part of the web or mobile client contract
- remote web and mobile clients should use a dedicated controller WebSocket when they become the active controller

## Discovery Endpoints

These are additive to the current remote API and support client-side device discovery.

### `GET /discovery/info`

Returns the same core metadata advertised over UDP broadcast.

Suggested fields:

- `hostId`
- `displayName`
- `remoteHost`
- `remotePort`
- `protocolVersion`
- `tls`

This allows clients to verify a discovered host over HTTP after receiving a UDP announcement.

Identity note:

- `hostId` is the canonical stable identity for a host and is generated once on first runtime boot
- `displayName` is descriptive metadata and is not required to be unique
- client-side host deduplication and pairing state must key on `hostId`, not `displayName`

### `GET /host/info`

Returns host metadata and capability flags.

Suggested fields:

- `hostId`
- `displayName`
- `version`
- `capabilities`
- `pairingMode`
- `tls`

Identity note:

- duplicate `displayName` values across hosts are valid
- host trust, saved-host records, and pairing association should be keyed by `hostId`

### `GET /ui/*`

Serves the daemon-local host web app used for approval and configuration.

Current note:

- the daemon-served browser assets are now considered compatibility surfaces
- maintained browser clients are developed outside this static asset bundle

### `POST /pairing/request`

Suggested request:

```json
{
  "deviceName": "Shubo iPhone",
  "deviceType": "mobile"
}
```

Suggested response:

```json
{
  "pairingId": "p_123",
  "code": "481923",
  "status": "pending"
}
```

### `POST /pairing/approve`

Host-local UI only.

Suggested request:

```json
{
  "pairingId": "p_123",
  "code": "481923"
}
```

Suggested response:

```json
{
  "deviceId": "d_123",
  "token": "opaque-token",
  "status": "approved"
}
```

### `GET /pairing/pending`

Host-local UI only. Returns pending pairing requests awaiting approval.

### `GET /sessions`

Returns lightweight session summaries.

Suggested fields per item:

- `sessionId`
- `provider`
- `workspaceRoot`
- `status`
- `lastActivityAt`
- `controllerClientId` if present
- `controllerKind` such as `host`, `remote`, or `none`
- `groupTags`

### `POST /sessions`

Creates and starts a new session.

Suggested request:

```json
{
  "provider": "codex",
  "workspaceRoot": "/Users/example/project",
  "title": "refactor-ui",
  "groupTags": ["frontend", "mvp"]
}
```

Suggested response:

```json
{
  "sessionId": "s_123",
  "status": "Starting"
}
```

### `GET /sessions/{sessionId}`

Returns session metadata without full terminal payload.

### `GET /sessions/{sessionId}/snapshot`

Returns:

- session metadata
- current sequence watermark
- bounded recent terminal tail
- recent file changes
- current git summary
- group tags

### `POST /sessions/{sessionId}/groups`

Adds, removes, or replaces group tags for a session.

Suggested request:

```json
{
  "mode": "add",
  "tags": ["frontend"]
}
```

Suggested `mode` values:

- `add`
- `remove`
- `set`

Suggested response:

```json
{
  "sessionId": "s_123",
  "groupTags": ["frontend", "mobile"]
}
```

### `GET /sessions/{sessionId}/tail?bytes=65536`

Returns recent terminal bytes and sequence metadata for reconnect and degraded recovery.

### `POST /sessions/{sessionId}/input`

Writes terminal input for the active controller.

Suggested request:

```json
{
  "data": "run tests\n"
}
```

### `POST /sessions/{sessionId}/resize`

Suggested request:

```json
{
  "cols": 120,
  "rows": 40
}
```

### `POST /sessions/{sessionId}/stop`

Stops the session gracefully, then forcefully if needed.

### `POST /sessions/{sessionId}/control`

Requests controller ownership or releases it back to the host/default path.

### `GET /sessions/{sessionId}/changes`

Returns recent aggregated file change events.

### `GET /sessions/{sessionId}/git`

Returns the latest git summary.

## WebSocket Events

Every event should carry:

- `type`
- `sessionId`
- `emittedAt`

### `terminal.output`

Suggested payload:

```json
{
  "type": "terminal.output",
  "sessionId": "s_123",
  "seqStart": 1040,
  "seqEnd": 1042,
  "data": "Running tests...\n"
}
```

### `terminal.truncated`

Used only when extreme conditions require data loss.

### `session.updated`

Includes lifecycle state changes and controller changes.

Suggested payload additions:

- `controllerClientId`
- `controllerKind`
- `terminalSize`
- `groupTags`

### `files.changed`

Contains a bounded batch of normalized file events.

### `git.changed`

Contains git summary deltas or full replacement snapshots.

### `session.exited`

Carries final exit code or error reason.

## Controller WebSocket

Interactive control should use a dedicated endpoint:

- `ws://HOST:18086/ws/sessions/{sessionId}/controller`

This endpoint is for the active controller only.

Behavior:

- opening the socket requests remote control immediately
- on success, the socket becomes the privileged low-latency controller lane
- on failure, the server sends a rejection frame and closes or otherwise terminates the attempt
- replay and inspection remain the job of the observer APIs

### Controller Server Frames

Binary frames:

- raw terminal output only

Text frames:

- `controller.ready`
- `controller.rejected`
- `controller.released`
- `session.exited`

Suggested `controller.ready` payload:

```json
{
  "type": "controller.ready",
  "sessionId": "s_123",
  "controllerKind": "remote"
}
```

Suggested `controller.rejected` payload:

```json
{
  "type": "controller.rejected",
  "sessionId": "s_123",
  "code": "control_unavailable",
  "message": "session already has an active controller"
}
```

Suggested `controller.released` payload:

```json
{
  "type": "controller.released",
  "sessionId": "s_123"
}
```

## WebSocket Commands

These are client-to-server messages over the attached session WebSocket.

Remote clients should continue using JSON text commands.

The daemon may also accept binary input frames for raw host-controller attach flows, but that is an implementation detail of the host attach path, not a required public client contract.

### `terminal.input`

Suggested payload:

```json
{
  "type": "terminal.input",
  "data": "run tests\n"
}
```

### `terminal.resize`

Suggested payload:

```json
{
  "type": "terminal.resize",
  "cols": 80,
  "rows": 24
}
```

### `session.stop`

Suggested payload:

```json
{
  "type": "session.stop"
}
```

### `session.control.request`

Suggested payload:

```json
{
  "type": "session.control.request"
}

## Controller WebSocket Client Frames

On the dedicated controller WebSocket:

- binary frames are raw terminal input bytes
- text frames are reserved for infrequent control messages

Suggested text commands:

```json
{
  "type": "terminal.resize",
  "cols": 120,
  "rows": 40
}
```

```json
{
  "type": "session.stop"
}
```

```json
{
  "type": "session.control.release"
}
```

### `session.groups.update`

Suggested payload:

```json
{
  "type": "session.groups.update",
  "mode": "add",
  "tags": ["frontend"]
}
```
```

### `session.control.release`

Suggested payload:

```json
{
  "type": "session.control.release"
}
```

## Authorization Rules

- health and pairing bootstrap routes may be unauthenticated
- host-local configuration and approval routes should be restricted to the local machine or an explicitly trusted path
- only authenticated paired devices may connect to session routes
- multiple observers may subscribe
- only one active controller may send terminal input
- unauthorized input attempts should return a clear error
- only the active controller may send resize commands
- host reclaim semantics should be explicit rather than inferred
- host-local attach uses an explicit reclaim shortcut instead of reclaiming on arbitrary stdin
- current local reclaim shortcut coverage targets common raw `Ctrl-]`, CSI-u, and
  `modifyOtherKeys` terminal encodings, but not every terminal-specific keyboard protocol

## Schema Design Rules

- preserve raw terminal stream semantically, but encode it safely on the wire
- keep incremental events small and replayable
- prefer additive schema evolution
- include enough sequence metadata for reconnect logic
- automatic redraw is safe on initial local attach, but automatic redraw on every control
  handoff is intentionally avoided because shell and TUI programs can respond with unstable
  scrollback or large blank regions

## Testing Requirements

- request validation tests for malformed input
- serialization tests for stable wire contracts
- attach/reconnect integration tests
- controller authorization tests
