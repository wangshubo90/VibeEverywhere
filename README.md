# Sentrits

Sentrits is a session runtime and control plane for AI coding CLIs.

It turns long-running Codex, Claude Code, Aider, and similar terminal sessions into supervised runtime objects that can be discovered, paired, observed, and controlled across clients.

## Vision

Sentrits is building toward a model where AI coding sessions are not just terminal pipes.

They should become:

- durable session nodes with stable identity
- observable execution surfaces with truthful inventory and supervision data
- remotely inspectable and controllable from web, mobile, and CLI clients
- eventually enriched by semantic monitoring layers that summarize what a session is doing without depending on raw ANSI alone

Near-term, the product is a daemon-managed PTY runtime with pairing, inventory, observe/control, and cross-client continuity.

Longer-term, the direction is:

- stronger session-node information
- richer semantic extraction and supervision
- better notifications and watch workflows
- packaging that treats the daemon as the product core and serves static client assets around it

## Architecture At A Glance

```text
                  ┌─────────────────────────────────────┐
                  │              Clients                │
                  │                                     │
                  │  CLI         Web         iOS        │
                  │  local       remote      remote     │
                  │  attach      observe/    observe/   │
                  │  observe     control     control    │
                  └────────────────┬────────────────────┘
                                   │
                      REST + WebSocket observer/control
                                   │
                     ┌─────────────▼─────────────┐
                     │      sentrits daemon      │
                     │                           │
                     │ session manager           │
                     │ auth + pairing            │
                     │ inventory + snapshots     │
                     │ terminal multiplexer      │
                     │ supervision + attention   │
                     └─────────────┬─────────────┘
                                   │
                            one PTY per session
                                   │
                      ┌────────────▼────────────┐
                      │ AI coding CLI process   │
                      │ Codex / Claude / Aider  │
                      └────────────┬────────────┘
                                   │
                    PTY output + workspace/git/process signals
```

More detail:

- `development_memo/system_architecture.md`
- `development_memo/architecture_refined.md`

## Observe / Control Design

Sentrits is built around one active controller and many observers.

- one real PTY per live session
- many observers may subscribe to the same session
- one controller owns terminal input and PTY resize at a time
- host-local attach is a privileged low-latency control path
- remote focused control uses a dedicated controller WebSocket
- canonical terminal snapshot/bootstrap data seeds clients before live deltas continue

This design keeps PTY semantics correct while still allowing:

- remote supervision
- focused intervention from another client
- truthful session inventory
- cross-client continuity

## Current Feature Set

Current working surface includes:

- daemon-managed PTY sessions for AI coding CLIs
- host-local CLI session management and local attach/observe/control
- remote pairing with bearer-token authorization
- stable persisted `hostId` for host identity
- truthful session inventory over REST and WebSocket
- one-controller enforcement across host and remote clients
- canonical terminal snapshot/bootstrap fields for client seeding
- remote observe/control used by maintained web and iOS clients
- bounded session persistence for recovered metadata and recent state
- coarse supervision and attention signals
- host-local admin web surface

Maintained client repos:

- Web: https://github.com/shubow-sentrits/Sentrits-Web
- iOS: https://github.com/shubow-sentrits/Sentrits-IOS

## Current Product Boundaries

Sentrits is not yet trying to solve:

- internet relay or brokered connectivity
- multi-user identity and account systems
- perfect semantic understanding of every terminal workflow
- full IDE-style editing inside clients
- complete monitoring subsystems on every supported platform

## Packaging Direction

Current packaging direction is daemon-first:

- runtime daemon as the product core
- CLI as the operator-facing management surface
- static web assets built separately and served by the runtime
- macOS and Debian first

See:

- `development_memo/packaging_architecture.md`

## Start Here

- `get_started.md`
- `VIBING.md`
- `development_memo/README.md`
- `development_memo/system_architecture.md`
- `development_memo/api_and_event_schema.md`
- `development_memo/packaging_architecture.md`
- `development_memo/mvp_checklist.md`
- `development_memo/future/session_terminal_multiplexer_and_semantic_runtime.md`

## Repository Layout

- `src/`
  - runtime daemon, CLI, HTTP/WS API, PTY/session runtime, pairing, persistence
- `include/`
  - public/runtime headers
- `frontend/`
  - in-repo host-admin workspace
- `development_memo/`
  - current architecture, packaging, MVP, and future design docs
- `prompts/`
  - focused implementation prompts and packaging/docs task prompts
- `deprecated/`
  - compatibility browser assets and older surfaces kept out of the main current path
