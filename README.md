# Sentrits

Sentrits-Core is a remote session runtime and control plane for AI coding CLIs.

The runtime is implemented in C++20 with CMake, Ninja, and Clang/LLVM.

## Current Runtime Shape

- macOS and Linux only
- PTY-backed session execution currently uses a shared POSIX `forkpty` backend behind an `IPtyProcess` factory seam
- one PTY per session
- many observers, one controller
- host-local `session start --attach` and `session attach` use a privileged low-latency local controller stream
- remote web and mobile clients use the HTTP and WebSocket observer/control API, including the dedicated remote controller WebSocket
- supervision state is live and exposed as `active`, `quiet`, or `stopped`
- attention state is conservative and derived from structured runtime signals
- pairing, session inventory, session create/stop, and group tags are part of the maintained surface
- file watching, process-tree inspection, and resource monitoring remain partially implemented seams rather than fully complete platform subsystems

## Current Runtime MVP

The current runtime MVP is:

- daemon-managed PTY sessions for AI coding CLIs
- host-local low-latency control through `session start --attach` and `session attach`
- remote observer and controller access for web and iOS clients
- one-controller enforcement across host and remote clients
- truthful session inventory with status, controller, supervision, and recent timestamps
- pairing and bearer-token-based remote access on the local network
- persisted per-host `hostId` generation on first runtime boot, with `displayName` treated as editable metadata rather than identity

The runtime is not yet trying to solve:

- server-side terminal screen snapshots for perfect first-frame rendering
- push-notification infrastructure
- multi-user account management
- internet relay or tunnel transport
- fully complete platform monitoring subsystems

## Repository Surfaces

- `sentrits`
  - runtime daemon, host-local admin/API surface, pairing, session management
- `frontend/`
  - maintained in-repo host-admin workspace
- `Sentrits-Web`
  - maintained browser remote client
- `Sentrits-IOS`
  - maintained iOS client
- `deprecated/web/`
  - legacy daemon-served plain HTML host and remote browser UIs kept for compatibility

## Start Here

- [VIBING.md](Sentrits-Core/VIBING.md)
- [build_and_test.md](Sentrits-Core/development_memo/build_and_test.md)
- [architecture_refined.md](Sentrits-Core/development_memo/architecture_refined.md)
- [api_and_event_schema.md](Sentrits-Core/development_memo/api_and_event_schema.md)
- [packaging_architecture.md](Sentrits-Core/development_memo/packaging_architecture.md)
- [development_memo/README.md](Sentrits-Core/development_memo/README.md)
