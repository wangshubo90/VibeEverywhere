# Session Terminal Multiplexer V1

## Purpose

This document defines the first implementation target for the session terminal multiplexer refactor.

It is intentionally narrower than the broader target-state memo.

V1 exists to solve the current product pain:

- one PTY has one size
- observer size mismatches break rendering
- attach and control handoff still depend too much on process repaint behavior
- re-attaching clients need better continuity than raw tail replay

This document is the implementation starting point.

## V1 Scope

V1 includes:

- one real PTY-backed process per session
- one active stdin owner per session
- one canonical headless terminal emulator per session
- many observer viewports per session
- current screen snapshot plus bounded scrollback
- additive migration that preserves the current API during rollout

V1 does not include:

- per-observer full emulator instances
- semantic monitor as a critical dependency
- provider-specific inference
- raw replay as a first-class compatibility surface
- tmux-style multi-writer control

## Core Model

The runtime is split into three planes.

### 1. Execution Plane

This is the real provider process.

- one PTY per session
- one authoritative PTY size
- one active stdin owner at a time
- only the stdin owner may resize the PTY

The execution plane remains the source of truth for the running process.

### 2. Canonical Render Plane

The runtime owns one headless terminal emulator per session.

It consumes PTY output at the authoritative PTY size and maintains:

- current screen
- cursor position
- active modes that matter for rendering
- bounded scrollback
- render revision

This emulator is the canonical terminal state for the session.

### 3. View Plane

Each attached observer has a viewport context.

Each viewport stores:

- `viewId`
- `sessionId`
- `principalId`
- viewport columns
- viewport rows
- horizontal offset
- vertical offset
- follow-bottom state
- follow-cursor state
- last delivered render revision

Viewport resize is not PTY resize.

Observer resize only changes viewport state.

## Invariants

V1 must preserve these rules.

1. One PTY-backed process per session.
2. One stdin owner per session.
3. Only the stdin owner may send PTY input.
4. Only the stdin owner may resize the PTY.
5. Observers never resize the PTY.
6. The runtime keeps one canonical rendered terminal state per session.
7. Newly attached clients can recover useful context from canonical state without forcing process repaint.
8. Current public REST and WebSocket contracts remain valid during migration.

## Geometry Rules

### Authoritative PTY Geometry

When a stdin owner exists:

- PTY columns and rows follow that owner's resize requests

When no stdin owner exists:

- the PTY keeps the last remembered size for that session

For a brand-new session with no prior remembered size:

- the runtime uses the configured default PTY size

### Observer Geometry

Observers have independent viewport size.

Observer size affects only observer rendering.

Observer views should:

- clip horizontally if narrower than the canonical screen
- allow horizontal scroll where useful
- support follow-cursor or follow-bottom behavior

Observers should not:

- rewrap the canonical terminal content to a different execution width
- mutate PTY size

## History Model

V1 continuity guarantee:

> A client can attach to an existing live session and recover the current screen plus useful recent scrollback without needing the provider process to repaint.

V1 retained history includes:

- current screen snapshot
- bounded scrollback

V1 does not require:

- unbounded history
- exact raw PTY replay as a first-class surface

Raw replay may be added later, but it is not part of the minimum multiplexer milestone.

## Host Attach

Local host attach must preserve the current low-latency control behavior.

That means:

- host attach keeps the privileged local controller lane for stdin and PTY resize
- host input is not routed through an observer-oriented slow path
- PTY output still feeds the canonical emulator
- host rendering should converge on canonical state over time, but the low-latency input path is preserved

For V1, host attach should be modeled as:

- one more client/view
- plus optional stdin ownership

The local host path remains special only for privilege and latency, not for canonical state ownership.

## API Direction For V1

V1 is additive first.

Current API remains the compatibility baseline.

That means existing clients may continue using:

- `GET /sessions`
- `GET /sessions/{sessionId}`
- `GET /sessions/{sessionId}/snapshot`
- observer WebSocket `terminal.output`
- observer WebSocket `session.updated`
- observer WebSocket `session.activity`
- controller WebSocket and controller events

V1 adds new observer-oriented state, rather than replacing current APIs immediately.

Recommended additions:

- `GET /sessions/{sessionId}/screen`
- `GET /sessions/{sessionId}/scrollback`
- additive snapshot fields for canonical render state
- render revision metadata

The implementation should not remove or silently redefine current event semantics during rollout.

## Data Model Additions

Recommended runtime-side additions:

### Session render state

- `ptyCols`
- `ptyRows`
- `renderRevision`
- `screenSnapshot`
- `scrollbackWindow`
- `stdinOwnerPrincipalId`

### Per-view state

- `viewId`
- `sessionId`
- `principalId`
- `viewportCols`
- `viewportRows`
- `horizontalOffset`
- `verticalOffset`
- `followBottom`
- `followCursor`
- `lastDeliveredRenderRevision`

## Implementation Phases

### Phase 1: terminology and ownership cleanup

- keep current wire-level controller behavior
- rename runtime internals toward stdin ownership
- preserve current controller sockets and privilege boundaries

### Phase 2: canonical emulator

- introduce a runtime-owned headless terminal emulator per session
- feed it from ordered PTY output
- store current screen and bounded scrollback

### Phase 3: viewport model

- add observer viewport state
- separate viewport resize from PTY resize
- stop observer flows from mutating PTY size

### Phase 4: snapshot continuity

- expose current screen snapshot
- expose bounded scrollback
- make attach/re-attach use canonical state instead of repaint dependence

### Phase 5: client migration

- migrate focused web view
- migrate focused iOS view
- migrate observer previews
- keep legacy event paths until both clients are stable

## Deferred For Later

- semantic monitor outputs beyond current supervision and attention
- raw replay as a first-class API
- agent observer-specific APIs
- per-observer full emulator state
- graph metadata beyond current session metadata

## Open Questions

These are still implementation decisions, not architecture blockers.

1. Which headless emulator implementation should the runtime use?
2. What bounded scrollback size is acceptable for desktop-hosted sessions?
3. Should a newly attached observer default to follow-bottom, follow-cursor, or preserve last known viewport state?
4. Should observer horizontal scroll be exposed in all clients, or only in focused views?
5. Should host attach rendering fully migrate to canonical state in V1, or remain partially raw until V2?

## Summary

V1 is not "tmux for Sentrits."

It is:

- one PTY
- one stdin owner
- one canonical runtime-owned terminal state
- many observer viewports
- current screen plus bounded scrollback
- additive migration that keeps the current API working while clients move over
