# Runtime Architecture

This document focuses on the current runtime-side architecture inside `Sentrits-Core`.

For the full runtime + client picture, start with:

- `system_architecture.md`

## Current Runtime Boundary

The runtime is organized around sessions.

A session currently owns:

- one provider process
- one PTY
- session metadata
- controller state
- bounded recent output state
- canonical terminal snapshot state
- coarse supervision and attention signals

The runtime is the source of truth for session execution, session inventory, and session control ownership.

## Main Runtime Components

### `HttpServer`

Responsibilities:

- serve host-local and remote listeners
- terminate REST routes
- terminate observer and controller WebSockets
- integrate pairing/auth checks
- deliver session inventory snapshots and per-session updates

Primary implementation:

- `src/net/http_server.cpp`
- `src/net/http_shared.cpp`
- `src/net/websocket_shared.cpp`

### `SessionManager`

Responsibilities:

- own the in-memory session registry
- create and recover session runtimes
- list sessions and produce summaries
- arbitrate controller ownership
- produce full session snapshots
- track timestamps and supervision/attention state

Primary implementation:

- `src/service/session_manager.cpp`
- `include/vibe/service/session_manager.h`

### `SessionRuntime`

Responsibilities:

- own the PTY-backed provider process
- poll PTY output
- accept controller input and resize
- maintain the current session record and output state

Primary implementation:

- `src/session/session_runtime.cpp`
- `include/vibe/session/session_runtime.h`

### Terminal Multiplexer

Responsibilities:

- maintain canonical terminal state derived from PTY output
- keep bounded history and visible lines
- generate current screen and viewport snapshots
- generate `bootstrapAnsi` for clients

Primary implementation:

- `src/session/terminal_multiplexer.cpp`

### Persistence

Responsibilities:

- persist host config and stable `hostId`
- persist pairing state
- persist lightweight session state for recovery

Primary implementation:

- `src/store/file_stores.cpp`
- `src/store/host_config_store.cpp`

### Auth And Pairing

Responsibilities:

- pairing request / approval / claim
- approved device/token persistence
- authorization for observe/control/admin actions

Primary implementation:

- `src/auth/default_pairing_service.cpp`
- `src/auth/default_authorizer.cpp`
- `src/net/local_auth.cpp`

## Current Data Flow

```text
provider process
  -> PTY
  -> SessionRuntime
  -> TerminalMultiplexer
  -> SessionManager
  -> REST snapshots / WS events

workspace file changes
  -> WorkspaceFileWatcher
  -> SessionManager
  -> session summaries / snapshots

pairing + auth state
  -> Auth services + file stores
  -> HTTP / WS authorization gates
```

## Observe / Control Model

Current product rule:

- many observers
- one active controller
- controller owns PTY input and resize
- host-local attach is privileged
- remote control uses a dedicated controller WebSocket

### Observer Lane

Observer clients use:

- `GET /sessions`
- `GET /sessions/{sessionId}/snapshot`
- `ws://.../ws/sessions/{sessionId}`

Observer lane responsibilities:

- session inventory and metadata
- attach-time replay/bootstrap
- live session updates
- passive terminal output consumption

### Controller Lane

Remote control uses:

- `ws://.../ws/sessions/{sessionId}/controller`

Host-local control uses:

- CLI attach paths backed by the local controller stream

Controller lane responsibilities:

- claim control
- send input
- send resize
- release control
- receive control-oriented terminal output

## Snapshot Model

Current runtime snapshots are additive and intended to seed clients without relying solely on raw replay.

Current snapshot fields include:

- `recentTerminalTail`
- `terminalScreen`
- `terminalViewport`
- `bootstrapAnsi`
- recent file and git state
- signals and controller metadata

Current JSON encoding lives in:

- `src/net/http_shared.cpp`
- `src/net/json.cpp`

## Control Ownership And Truthfulness

`SessionManager` is responsible for keeping controller truth coherent.

Important current behaviors:

- one controller at a time
- remote controller can claim active ownership
- host-local control is privileged
- release returns the session toward host ownership semantics
- controller changes update session summaries and timestamps

## Current Platform Shape

Current supported runtime shape:

- macOS
- Linux
- POSIX `forkpty` backend selected through a factory seam

Important note:

- there are wider seams for monitoring and richer platform integration
- but many of those remain partial or future-oriented rather than fully complete subsystems today

## Future Direction

The next meaningful architecture layer is not more raw PTY forwarding.

The next layer is:

- richer session-node information
- more reliable semantic monitoring
- stronger supervision summaries

Relevant future docs:

- `future/session_terminal_multiplexer_and_semantic_runtime.md`
- `future/session_signal_map.md`
- `future/pty_semantic_extractor.md`
