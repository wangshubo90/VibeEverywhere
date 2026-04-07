# System Architecture

This document describes the current end-to-end Sentrits architecture across the runtime, CLI, and maintained clients.

Code is the source of truth. If this document disagrees with implementation, update the document to match the code.

## Product Shape

Sentrits is a session runtime and control plane for AI coding CLIs.

The product is centered on:

- one daemon-managed PTY-backed runtime per session
- host-local CLI management and low-latency local attach/control
- remote observe and remote control through REST and WebSocket APIs
- session supervision data that remains useful even when no client is actively attached

Clients do not connect to machines as their primary abstraction. They connect to sessions exposed by a host runtime.

## High-Level Topology

```text
                         ┌──────────────────────────────┐
                         │        Sentrits Clients      │
                         │                              │
                         │  Web: inventory, explorer,   │
                         │       focused observe/control│
                         │                              │
                         │  iOS: pairing, inventory,    │
                         │       explorer, focused term │
                         │                              │
                         │  CLI: list/show/observe plus │
                         │       local attach/control   │
                         └──────────────┬───────────────┘
                                        │
                          REST + WS observer/control API
                                        │
                    ┌───────────────────▼───────────────────┐
                    │             sentrits daemon            │
                    │                                        │
                    │  HTTP API   Pairing/Auth   Inventory   │
                    │  WS API     Host config    Session mgmt│
                    │                                        │
                    │  Terminal multiplexer   Signal model   │
                    │  Snapshot/bootstrap     Attention      │
                    └───────────────┬────────────────────────┘
                                    │
                             one PTY per session
                                    │
                         ┌──────────▼──────────┐
                         │  AI coding process  │
                         │ Codex / Claude Code │
                         │ Aider / others      │
                         └──────────┬──────────┘
                                    │
                     ┌──────────────┼──────────────┐
                     │              │              │
                     ▼              ▼              ▼
              PTY output      workspace watcher  git/process/resource seams
              and input       and file changes   for supervision signals
```

## Runtime Responsibilities

The runtime owns execution truth.

Current runtime responsibilities:

- create, recover, list, inspect, and stop sessions
- own exactly one PTY and one provider process per live session
- expose session inventory over REST and WebSocket
- expose observer and controller WebSocket lanes
- enforce one active controller at a time
- provide remote pairing and bearer-token authorization
- persist host identity, pairing state, and lightweight session state
- derive coarse supervision and attention signals from runtime activity
- expose canonical terminal snapshot/bootstrap data for clients

The runtime is not just a PTY forwarder. It is the supervision-oriented session system of record.

## Runtime Components

### `HttpServer` and HTTP/WS API

Primary responsibilities:

- serve host-local admin and remote APIs
- terminate REST requests
- terminate observer and controller WebSockets
- gate access through authorization checks
- expose host identity and pairing endpoints

Current API surfaces live mainly in:

- `src/net/http_server.cpp`
- `src/net/http_shared.cpp`
- `src/net/json.cpp`

### `SessionManager`

Primary responsibilities:

- own the session registry
- create and recover `SessionRuntime` instances
- arbitrate controller assignment
- produce session summaries and snapshots
- manage lifecycle transitions, timestamps, and inventory truthfulness
- derive current attention/supervision summaries from runtime state

Current implementation:

- `src/service/session_manager.cpp`
- `include/vibe/service/session_manager.h`

### `SessionRuntime`

Primary responsibilities:

- own one launched provider process and its PTY
- poll PTY output and append it to session state
- accept input and resize requests for the active controller
- expose process lifecycle and current terminal state

Current implementation:

- `src/session/session_runtime.cpp`
- `src/session/posix_pty_process.cpp`
- `include/vibe/session/session_runtime.h`

### Terminal Multiplexer

Primary responsibilities:

- turn PTY output into a canonical terminal model
- maintain bounded history and visible screen state
- expose current screen snapshot and per-view viewport snapshot
- generate bootstrap ANSI used by web and iOS clients

Current implementation:

- `src/session/terminal_multiplexer.cpp`

This is the current answer to first-frame rendering, history continuity, and cross-client viewport seeding.

## Observe / Control Design

The observe/control model is central to the product.

Current rule set:

- many observers may attach to a session
- exactly one controller may mutate a session at a time
- controller owns terminal input and PTY resize
- observers receive state updates and terminal output but do not own PTY size

### Observe Lane

Observer clients use:

- `GET /sessions`
- `GET /sessions/{sessionId}/snapshot`
- `ws://.../ws/sessions/{sessionId}`

Observer lane responsibilities:

- attach-time replay or snapshot/bootstrap seed
- incremental terminal output
- session metadata updates
- controller-state changes
- inventory-safe session supervision

### Control Lane

Remote active control uses:

- `ws://.../ws/sessions/{sessionId}/controller`

Host-local active control uses:

- CLI attach paths backed by the local privileged controller stream

Control lane responsibilities:

- request control
- send terminal input
- send resize
- release control
- receive low-latency control output

### Host vs Remote Control

Current product behavior:

- local host attach is privileged and low-latency
- remote controller WebSocket is the active remote mutation path
- host may reclaim control
- controller changes are reflected in inventory and session updates

This model is implemented rather than merely planned. The relevant behavior is visible in:

- `src/cli/daemon_client.cpp`
- `src/net/http_server.cpp`
- `src/service/session_manager.cpp`

## Terminal Snapshot and Bootstrap Model

Current client continuity depends on canonical snapshot data rather than raw tail text alone.

Current snapshot shape includes:

- `recentTerminalTail`
- `terminalScreen`
- `terminalViewport`
- `bootstrapAnsi`

This matters because clients need:

- truthful preview content
- first useful frame on open/focus
- history continuity across resize and reconnect
- control handoff without depending entirely on a fresh repaint from the app inside the PTY

Current JSON emission for these fields is implemented in:

- `src/net/http_shared.cpp`
- `src/net/json.cpp`

## PTY and Multiplexing Model

Sentrits currently uses one PTY per session, not one PTY per client.

That PTY is shared at the execution level but not at the view-model level.

The important split is:

- execution truth: one provider process, one PTY, one active input owner
- render truth: one canonical terminal state plus per-view snapshot/bootstrap data

This keeps PTY semantics correct while still allowing:

- many observers
- different client layouts
- focused control without restarting the process

## Current Client Roles

### Host CLI

The CLI is not just a test harness. It is a maintained management and local-control surface.

Current responsibilities:

- run the daemon
- list and show sessions
- start and stop sessions
- local attach and observe
- read host status
- clear inactive sessions

Primary entrypoint:

- `src/main.cpp`

### Web Client

Maintained browser client repo:

- `https://github.com/shubow-sentrits/Sentrits-Web`

Current role:

- paired host inventory
- explorer/preview workflow
- focused observe/control
- multi-host supervision and grouping

### iOS Client

Maintained iOS client repo:

- `https://github.com/shubow-sentrits/Sentrits-IOS`

Current role:

- pairing
- inventory
- explorer and focused session view
- observe/control from mobile
- local notifications for selected runtime events

## Discovery and Identity

Current host identity model:

- `hostId` is stable and persisted on first boot
- `displayName` is descriptive metadata only
- pairing and saved-host identity should follow `hostId`, not display name

Current discovery model:

- runtime advertises discovery info over UDP
- runtime exposes `GET /discovery/info`
- browser clients may require a helper bridge for UDP discovery
- native clients can consume UDP discovery directly

## Packaging Direction

Current packaging direction is daemon-first.

Target packaging shape:

- daemon/runtime as the core product
- CLI as the operator-facing management surface
- static web assets built separately and served by the runtime
- `launchd` on macOS
- `systemd` and `.deb` packaging on Debian first

See:

- `packaging_architecture.md`

## Future Direction

Current runtime already has terminal multiplexing and snapshot/bootstrap support, but the future design still expands beyond transport.

The most important future layers are:

- richer session-node information for inventory and graph views
- semantic extraction from PTY, workspace, and git/process signals
- monitoring that is more useful than raw terminal activity alone

High-level future direction:

```text
PTY/process truth
  -> canonical terminal state
  -> session signal extraction
  -> semantic monitoring layer
  -> session-node summaries / attention / graph-ready state
  -> client inventory, previews, notifications, intervention UX
```

Relevant future design docs:

- `future/session_terminal_multiplexer_and_semantic_runtime.md`
- `future/session_signal_map.md`
- `future/pty_semantic_extractor.md`

These future docs are directional. They are not claims about the full current implementation.
