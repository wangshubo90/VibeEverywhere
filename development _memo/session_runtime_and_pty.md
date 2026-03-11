# Session Runtime and PTY Design

This document focuses on the most critical runtime boundary: one provider process attached to one PTY, owned by one session runtime.

The PTY belongs to the session, not to the host terminal UI.

## Session Runtime Data Model

Suggested fields:

- `SessionId id`
- `ProviderType provider`
- `std::filesystem::path workspace_root`
- `SessionState state`
- `std::optional<ProcessId> pid`
- `std::unique_ptr<IPtyProcess> pty`
- `SessionOutputBuffer output_buffer`
- `RecentFileChanges recent_file_changes`
- `GitSummary last_git_summary`
- `std::optional<ClientId> controller_client`
- `SessionTimestamps timestamps`
- `TerminalSize active_terminal_size`

Keep mutable runtime state concentrated here rather than scattered across networking code.

## Session Lifecycle

States:

- `Created`
- `Starting`
- `Running`
- `AwaitingInput`
- `Exited`
- `Error`

Rules:

- only valid transitions should be allowed
- state transitions should emit structured events
- exit path should capture final status and timestamps
- failed launch should never appear as `Running`
- controller changes should emit structured session updates

This state machine should be built as a pure logic component first and covered by unit tests.

## PTY Abstraction

Define an interface along these lines:

- `start(LaunchSpec) -> StartResult`
- `write(bytes)`
- `resize(cols, rows)`
- `terminate()`
- `poll_exit()`
- output callback or read loop entry point

Keep the abstraction responsible only for PTY/process mechanics. Session policy belongs one layer above.

Current implementation note:

- macOS and Linux currently share a POSIX `forkpty` backend
- selection happens through a factory seam that returns `IPtyProcess`
- `SessionManager` and CLI entrypoints should depend on the interface or factory, not a concrete POSIX class
- platform-specific headers and link requirements belong in the backend implementation or CMake

## Launch Specification

Each provider launch should be explicit:

- executable path
- argument vector
- environment overrides
- workspace root as current working directory
- terminal size

The launch specification should be serializable enough for logging and testing, with secrets redacted if later needed.

The launch spec should include a default PTY size used before any client takes control.

## Output Ingestion

PTY ingestion rules:

- read raw bytes continuously
- append to `SessionOutputBuffer` immediately
- never wait on WebSocket send completion
- notify dispatcher with lightweight metadata only

The PTY reader path should do as little work as possible beyond safe buffering and event notification.

## Session Output Buffer

Core properties:

- bounded byte capacity
- append-only logical sequence progression
- eviction of oldest data when capacity is exceeded
- efficient recent-tail retrieval by byte limit
- ability to read from a sequence watermark when available

Recommended initial configuration:

- capacity: 8 MB per session
- flush batch target: 16 KB
- flush interval: 50 ms

These should be configurable rather than hard-coded.

## Controller and Resize Semantics

Because the provider process runs inside a PTY, terminal size affects the bytes emitted by the process.

Therefore:

- only one terminal size can be active for a session at any moment
- the current controller owns PTY resize
- observers render the resulting byte stream but do not set PTY size
- if a remote client takes control from the host, the host view must follow the controller-driven PTY size
- if control returns to the host or no controller remains, the PTY may snap back to a configured default size

## Per-Client Delivery State

Each subscribed client should track:

- `last_acked_seq`
- `pending_bytes`
- `delivery_mode`
- `subscription_time`

Delivery modes:

- `Live`
- `Catchup`
- `Degraded`

The dispatcher should be able to switch clients to degraded delivery without altering session execution.

## Reconnect Behavior

Recommended attach flow:

1. client requests session snapshot
2. server returns metadata, recent tail, and current sequence watermark
3. client subscribes to live stream
4. if a sequence gap is detected, client requests tail or snapshot again

If the attaching client becomes controller, it should also send an initial resize command so the PTY matches its viewport.

This avoids replaying the full history while preserving a reliable recent context.

## Test Priorities

First tests to write:

- lifecycle transition validity
- ring buffer eviction and tail retrieval
- sequence number monotonicity
- PTY fixture round-trip for input and output
- slow-client degradation policy
- reconnect from sequence watermark
- controller handoff and return-to-host behavior
- PTY resize behavior when controller changes
