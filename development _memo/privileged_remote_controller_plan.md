# Privileged Remote Controller Plan

This document defines the next transport step after the local host controller fix.

The goal is:

- keep the current local privileged controller stream over Unix domain socket
- add an equivalent privileged controller lane for remote web and iOS clients
- keep the existing observer API for inventory, replay, inspection, and passive viewing

## Core Model

Per session:

- one PTY
- many observers
- one active controller

Transport lanes:

- local host controller lane
  - Unix domain socket
  - raw input/output
  - already implemented
- remote controller lane
  - dedicated authenticated WebSocket
  - raw terminal bytes plus minimal control frames
- observer lane
  - current HTTP and WebSocket model
  - replay, summaries, inventory, activity, inspection, passive session viewing

The key rule is:

- the active controller's input and return output must not share the observer hot path

## API Shape

### Observer APIs

These remain the same:

- `GET /sessions`
- `GET /sessions/{sessionId}`
- `GET /sessions/{sessionId}/snapshot`
- `GET /sessions/{sessionId}/tail`
- `GET /sessions/{sessionId}/changes`
- `GET /sessions/{sessionId}/git`
- `ws://HOST:18086/ws/sessions/{sessionId}`
- `ws://HOST:18086/ws/overview`

Observer semantics remain:

- JSON events
- replayable terminal output
- session/activity/status updates
- slow-client protection and degradation

### New Remote Controller WebSocket

Add a dedicated controller endpoint:

- `ws://HOST:18086/ws/sessions/{sessionId}/controller`

Authentication:

- same bearer token rules as the current session WebSocket

Connection rules:

- opening this socket requests control immediately
- if control is granted, this socket becomes the session's active remote controller lane
- if control is denied, the socket returns a rejection frame and closes or remains idle based on final policy

Recommended first policy:

- reject and close if another controller is active

### Server To Client Frames

Binary frames:

- raw terminal output only

Text frames:

- rare control and lifecycle events only

Suggested text frames:

```json
{
  "type": "controller.ready",
  "sessionId": "s_123",
  "controllerKind": "remote",
  "terminalSize": { "cols": 120, "rows": 40 }
}
```

```json
{
  "type": "controller.rejected",
  "sessionId": "s_123",
  "code": "control_unavailable",
  "message": "session already has an active controller"
}
```

```json
{
  "type": "session.exited",
  "sessionId": "s_123",
  "status": "Exited"
}
```

```json
{
  "type": "controller.released",
  "sessionId": "s_123"
}
```

Do not send these on the hot path:

- `session.activity`
- periodic `session.updated`
- replay-oriented `terminal.output`

Those remain on the observer lane.

### Client To Server Frames

Binary frames:

- raw terminal input bytes only

Text frames:

- `terminal.resize`
- `session.stop`
- `session.control.release`

Suggested text frames:

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

## Client Behavior

### Web Client

Use two connections when a user actively controls a session:

- observer session WebSocket
  - inventory/detail state
  - session metadata
  - replay/tail
  - inspection panes
- controller WebSocket
  - live terminal input/output
  - control lifecycle

Behavior:

- observer-only open remains supported
- requesting control upgrades the live terminal path to the controller WebSocket
- releasing control falls back to observer-only mode

### iOS Client

Use the same split:

- observer WebSocket for session state and replay
- controller WebSocket for the focused interactive terminal

Behavior:

- inventory/explorer can stay observer-driven
- focused session requests control by opening the controller socket
- focused session input should be disabled unless `controller.ready` has been received

## Runtime Behavior

The runtime should treat the remote controller lane the same way it now treats the local privileged controller lane:

- immediate PTY input forwarding
- immediate PTY output forwarding
- no observer batching in front of the controller
- maintenance polling must not duplicate PTY work for a session owned by an active privileged controller lane

This likely means:

- refactor the current local controller session into a reusable privileged controller bridge
- provide two transports into that bridge:
  - local Unix socket transport
  - remote WebSocket transport

## Replay And Recovery

The controller lane should stay live-only.

Recommended policy:

- observer lane remains responsible for replay and catch-up
- clients should fetch `snapshot` or `tail` before opening the controller lane when needed
- the controller lane begins forwarding from "now", not from an observer-style replay backlog

This keeps the controller path simple and low-latency.

## Rollout Plan

### Phase 1

Runtime:

- introduce the reusable privileged controller bridge
- add remote controller WebSocket endpoint
- make active remote controller sessions skip observer maintenance polling just like local privileged sessions

Web:

- add controller WebSocket client for focused terminal control
- keep observer session tab state unchanged

iOS:

- add controller WebSocket client in focused session flow
- keep inventory and explorer observer-driven

### Phase 2

Runtime:

- tune close/reconnect behavior
- add controller-specific metrics and traces

Web and iOS:

- polish reconnect behavior
- handle control loss cleanly
- improve terminal UX for live controller mode

## Success Criteria

- remote iOS and web control feels materially closer to `local-pty`
- observer tabs remain stable and replay-capable
- one controller per session remains enforced
- local host attach continues using the Unix socket path unchanged
