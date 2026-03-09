# API and Event Schema Draft

This document translates the initiative API into a more implementation-ready contract.

## REST Endpoints

### `GET /host/info`

Returns host metadata and capability flags.

Suggested fields:

- `hostId`
- `displayName`
- `version`
- `capabilities`

### `GET /sessions`

Returns lightweight session summaries.

Suggested fields per item:

- `sessionId`
- `provider`
- `workspaceRoot`
- `status`
- `lastActivityAt`
- `controllerClientId` if present

### `POST /sessions`

Creates and starts a new session.

Suggested request:

```json
{
  "provider": "codex",
  "workspaceRoot": "/Users/example/project",
  "title": "refactor-ui"
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

### `files.changed`

Contains a bounded batch of normalized file events.

### `git.changed`

Contains git summary deltas or full replacement snapshots.

### `session.exited`

Carries final exit code or error reason.

## Authorization Rules

- only authenticated paired devices may connect
- multiple observers may subscribe
- only one active controller may send terminal input
- unauthorized input attempts should return a clear error

## Schema Design Rules

- preserve raw terminal stream in `data`
- keep incremental events small and replayable
- prefer additive schema evolution
- include enough sequence metadata for reconnect logic

## Testing Requirements

- request validation tests for malformed input
- serialization tests for stable wire contracts
- attach/reconnect integration tests
- controller authorization tests
