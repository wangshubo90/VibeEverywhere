# MVP Checklist

This checklist is the current runtime-centered MVP view for Sentrits.

It separates:

- the working product baseline
- the next-release MVP priorities
- explicit non-goals

## Current Working Baseline

The current codebase already supports these baseline flows.

### 1. Daemon Boot And Host Identity

Acceptance:

- the daemon starts and serves both host-local and remote listeners
- the host exposes stable `hostId` and editable `displayName`
- host info and discovery info are reachable from clients

### 2. Session Lifecycle

Acceptance:

- the daemon can create, list, inspect, stop, and clear sessions
- session inventory remains truthful across create/stop/recovery flows
- ended sessions remain visible until cleared

### 3. Host-Local CLI Observe / Control

Acceptance:

- `session start --attach` works as the local host control path
- `session attach` and `session observe` work through the daemon
- host-local attach remains low-latency and controller-aware

### 4. Remote Pairing And Authorization

Acceptance:

- a remote client can request pairing
- pairing can be approved from the host-local admin surface
- approved clients receive bearer-token access
- unauthorized clients cannot observe or control sessions

### 5. Remote Observer Flow

Acceptance:

- a paired client can list sessions
- a paired client can fetch a session snapshot
- observer WebSocket attach provides session updates and terminal output
- remote inventory can remain useful without local terminal access

### 6. Remote Controller Flow

Acceptance:

- a paired client can request control
- only one controller is active at a time
- remote controller WebSocket drives input and resize
- release and reclaim behavior is surfaced in session state

### 7. Canonical Snapshot / Bootstrap Data

Acceptance:

- snapshots expose terminal bootstrap data suitable for client seeding
- snapshot fields include current terminal state, not only raw tail text
- client continuity no longer depends entirely on fresh PTY repaint

### 8. Supervision Signals

Acceptance:

- session summaries expose controller, lifecycle, timestamps, and coarse attention signals
- supervision state remains distinct from raw transport bytes
- inventory views can surface meaningful session status even when the terminal is not focused

## Next-Release MVP Priorities

These are the next practical priorities rather than speculative redesign work.

### 1. Packaging Pipeline

Acceptance:

- `Sentrits-Core` owns the release pipeline
- runtime build and tests run first
- packaged static web assets are staged from `https://github.com/shubow-sentrits/Sentrits-Web`
- macOS and Debian packaging paths are documented and exercised

### 2. Cleaner Runtime Docs And Operator Onboarding

Acceptance:

- landing page, quickstart, architecture, and packaging docs match the code
- stale client shadow docs are removed from `Sentrits-Core`
- `get_started.md` remains a clean build/test/CLI document

### 3. Stronger Session Inventory Truth

Acceptance:

- controller, attention, and session state remain operationally trustworthy
- host and remote inventory remain consistent under attach/reclaim/release flows
- recovery after daemon restart is explicit and non-misleading

### 4. Better Supervision Layer

Acceptance:

- file, git, and controller-change signals remain consistent enough for clients
- attention modeling continues to improve without forcing clients to parse raw PTY bytes
- session summaries remain useful for watch/intervene workflows

## Explicit Non-Goals For This MVP

- multi-user account systems
- internet relay or brokered remote transport
- perfect semantic understanding of every CLI/TUI workflow
- IDE-style editing in clients
- full native desktop-shell packaging as the primary product shape
- replacing the daemon/CLI model with a GUI-app-first architecture

## Notes

Future semantic-monitoring and session-node work is important, but it should be treated as the next design horizon, not as a claim that the current MVP already provides full semantic understanding.

See:

- `system_architecture.md`
- `future/session_terminal_multiplexer_and_semantic_runtime.md`
- `future/session_signal_map.md`
