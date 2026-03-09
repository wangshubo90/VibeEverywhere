# Session Runtime and PTY Design

This document focuses on the most critical runtime boundary: one provider process attached to one PTY, owned by one session runtime.

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

## Launch Specification

Each provider launch should be explicit:

- executable path
- argument vector
- environment overrides
- workspace root as current working directory
- terminal size

The launch specification should be serializable enough for logging and testing, with secrets redacted if later needed.

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

This avoids replaying the full history while preserving a reliable recent context.

## Test Priorities

First tests to write:

- lifecycle transition validity
- ring buffer eviction and tail retrieval
- sequence number monotonicity
- PTY fixture round-trip for input and output
- slow-client degradation policy
- reconnect from sequence watermark
