# iOS Client API Contract

This document restates the current daemon contract from the point of view of an iOS Swift client.

The goal is not to define a final public API. The goal is to give the iOS client a stable MVP target.

## Transport Model

The host daemon exposes two listener surfaces:

- local host admin listener
  - default: `127.0.0.1:18085`
  - never intended for remote iOS client use
- remote client listener
  - default: `0.0.0.0:18086`
  - this is the iOS client target

For iOS MVP, the app should assume:

- REST over `http://HOST:18086`
- WebSocket over `ws://HOST:18086`
- later migration to `https://` and `wss://` is expected

## Authentication Model

The iOS client uses the remote listener only.

Authentication flow:

1. client sends `POST /pairing/request`
2. host user approves on local host admin UI
3. approval returns a bearer token
4. iOS client stores bearer token securely
5. subsequent REST calls send `Authorization: Bearer <token>`
6. WebSocket attaches can send:
   - `Authorization: Bearer <token>`
   - or `?access_token=<token>` if platform constraints require it

Storage recommendation for iOS:

- store bearer token in Keychain
- store last connected host address separately

## REST Endpoints For iOS MVP

### `GET /health`

Used for reachability only.

Response body:

```text
ok
```

### `GET /host/info`

Used to show host identity and remote capability flags.

Important fields currently expected:

- `hostId`
- `displayName`
- `version`
- `capabilities`
- `pairingMode`
- `tls`

### `POST /pairing/request`

Request:

```json
{
  "deviceName": "Shubo iPhone",
  "deviceType": "mobile"
}
```

Response:

```json
{
  "pairingId": "p_123",
  "code": "481923",
  "status": "pending"
}
```

Notes:

- pending requests expire
- approval happens only on the host admin UI

### `GET /sessions`

Authorization required.

Returns lightweight session summaries.

Current fields the client should treat as meaningful:

- `sessionId`
- `provider`
- `workspaceRoot`
- `title`
- `status`
- `controllerKind`
- `controllerClientId` if present

Client guidance:

- use `status` and `controllerKind` in the list UI
- do not assume session ids are contiguous

### `POST /sessions`

Authorization required.

Request:

```json
{
  "provider": "codex",
  "workspaceRoot": "/Users/example/project",
  "title": "refactor-ui"
}
```

Optional explicit command override:

```json
{
  "provider": "claude",
  "workspaceRoot": "/Users/example/project",
  "title": "claude-session",
  "command": [
    "/opt/homebrew/bin/claude",
    "--print"
  ]
}
```

Response:

```json
{
  "sessionId": "s_123",
  "status": "Starting"
}
```

### `GET /sessions/{sessionId}`

Authorization required.

Returns lightweight session metadata.

Use for detail refresh when the WebSocket is not attached.

### `GET /sessions/{sessionId}/snapshot`

Authorization required.

Returns:

- session metadata
- current terminal output sequence watermark
- recent terminal tail

For iOS MVP, this is mainly useful for reconnect recovery or a fallback detail view.

### `GET /sessions/{sessionId}/tail?bytes=N`

Authorization required.

Returns recent terminal output and sequence metadata.

Use case:

- reconnect replay
- degraded recovery when the live stream was interrupted

### `POST /sessions/{sessionId}/stop`

Authorization required.

Stops the real host-side session process.

The app should treat this as destructive.

### `POST /sessions/{sessionId}/input`

Authorization required.

This exists, but the preferred interactive path for the iOS client is WebSocket commands after attach.

## WebSocket Attach

Endpoint:

- `ws://HOST:18086/ws/sessions/{sessionId}`

Attach rules:

- requires authorization
- attaches as an observer first
- client can later request control

Attach-time behavior:

- server emits an initial `session.updated`
- server then emits attach-time terminal replay as `terminal.output`
- server then continues incremental output

## WebSocket Server Events

All event payloads are JSON.

### `session.updated`

Fields currently important to the client:

- `type`
- `sessionId`
- `status`
- `controllerKind`
- `controllerClientId` if present

Use it to:

- update list/detail state
- enable or disable input affordances
- react to control handoff

### `terminal.output`

Important fields:

- `type`
- `sessionId`
- `seqStart`
- `seqEnd`
- `dataEncoding`
- `dataBase64`

Important note:

- terminal payload is binary-safe
- the client must decode `dataBase64`
- do not assume UTF-8 text

### `session.exited`

Important fields:

- `type`
- `sessionId`
- `status`

Use it to:

- mark the session ended
- detach or disable input UI

### `error`

Important fields:

- `type`
- `sessionId`
- `code`
- `message`

Use it for:

- control rejection
- malformed command feedback

## WebSocket Client Commands

### `session.control.request`

Request control:

```json
{
  "type": "session.control.request",
  "kind": "remote"
}
```

### `session.control.release`

Release control:

```json
{
  "type": "session.control.release"
}
```

### `terminal.input`

Send raw terminal input:

```json
{
  "type": "terminal.input",
  "data": "run tests\n"
}
```

### `terminal.resize`

Send current terminal dimensions:

```json
{
  "type": "terminal.resize",
  "cols": 80,
  "rows": 24
}
```

### `session.stop`

```json
{
  "type": "session.stop"
}
```

## iOS Client State Model

A practical Swift state model for MVP:

- `HostConnection`
  - host address
  - remote port
  - token
  - connection status
- `SessionListItem`
  - id
  - title
  - provider
  - status
  - controller kind
- `SessionDetail`
  - summary
  - recent tail
  - websocket status
- `TerminalStreamState`
  - next expected sequence
  - control state
  - connected/disconnected/reconnecting

## Recommended Error Handling

The iOS client should distinguish:

- host unreachable
- unauthorized token
- pairing pending
- session no longer exists
- connected but observer-only
- session exited

Do not collapse all of these into one generic failure banner.

## API Stability Guidance

For iOS MVP, treat these fields as stable enough to code against:

- `sessionId`
- `status`
- `controllerKind`
- `controllerClientId`
- `type`
- `dataEncoding`
- `dataBase64`

Treat other payload details as additive and tolerate unknown fields.
