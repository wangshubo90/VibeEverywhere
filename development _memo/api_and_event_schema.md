# API and Event Schema Draft

This document translates the initiative API into a more implementation-ready contract.

## REST Endpoints

All non-health endpoints should be designed to support bearer-token authorization in the MVP.

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

### `GET /host/info`

Returns host metadata and capability flags.

Suggested fields:

- `hostId`
- `displayName`
- `version`
- `capabilities`
- `pairingMode`
- `tls`

### `GET /ui/*`

Serves the daemon-local host web app used for approval and configuration.

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

## WebSocket Commands

These are client-to-server messages over the attached session WebSocket.

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

## Schema Design Rules

- preserve raw terminal stream semantically, but encode it safely on the wire
- keep incremental events small and replayable
- prefer additive schema evolution
- include enough sequence metadata for reconnect logic

## Testing Requirements

- request validation tests for malformed input
- serialization tests for stable wire contracts
- attach/reconnect integration tests
- controller authorization tests
