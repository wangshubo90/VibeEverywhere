# Refined Architecture

This document refines the initiative-level architecture into implementation-oriented boundaries.

## System Boundary

The system is organized around sessions, not machines.

A session is defined by:

- session id
- workspace root
- provider type
- runtime status
- lightweight persisted metadata

The host is the owner of session execution and observation. Clients are subscribers and optional controllers.

## Primary Runtime Components

### API Layer

Responsibilities:

- expose REST routes
- authenticate requests
- manage WebSocket session subscriptions
- translate between wire schema and internal service calls

### SessionManager

Responsibilities:

- create, load, list, stop, and inspect sessions
- own session registry
- arbitrate controller assignment
- coordinate SessionRuntime, SnapshotStore, FileWatcher, and GitInspector

### SessionRuntime

Responsibilities:

- manage a single provider process through PTY
- capture raw output
- append to SessionOutputBuffer
- accept terminal input and resize requests
- report lifecycle changes

### SessionOutputBuffer

Responsibilities:

- store bounded recent terminal bytes
- assign monotonic sequence numbers
- assemble replayable tail views
- expose efficient reads for live delivery and catch-up

### ClientDispatcher

Responsibilities:

- track per-client delivery state
- batch output frames
- enforce slow-client degradation rules
- isolate client backpressure from PTY reading

### SnapshotStore

Responsibilities:

- persist lightweight session metadata
- persist recent terminal tail when needed
- restore session registry after host restart

### FileWatcher

Responsibilities:

- observe workspace file changes
- aggregate noisy change bursts
- produce normalized file-change events

### GitInspector

Responsibilities:

- sample repository status periodically or on demand
- emit summarized git state

### AuthManager

Responsibilities:

- device pairing
- token validation
- authorization policy checks

## Recommended Internal Flow

```text
Client
  -> API Layer
  -> SessionManager
  -> SessionRuntime / SnapshotStore / AuthManager

Provider Process
  -> PTY
  -> SessionRuntime
  -> SessionOutputBuffer
  -> ClientDispatcher
  -> WebSocket subscribers

Workspace
  -> FileWatcher / GitInspector
  -> SessionManager
  -> API Layer / WebSocket subscribers
```

## Threading Model

Recommended first implementation:

- one async event loop for network and timers
- one PTY reader task or thread per active session
- bounded queues or direct append path into `SessionOutputBuffer`
- dispatcher work scheduled independently from PTY reads

This gives a clean separation between high-priority PTY ingestion and comparatively lower-priority client delivery.

## Platform Abstractions

Keep these behind narrow interfaces:

- PTY allocation and child launch
- signal/termination behavior
- file system watching
- secure credential storage if later required

This project can start with a strong macOS/Linux path while still avoiding platform-specific leakage into session orchestration logic.

## Persistence Model

Persist:

- session metadata
- workspace root
- provider type
- last known status
- recent file changes
- git summary
- bounded recent terminal tail

Do not persist:

- active PTY handles
- full process state
- full terminal history

## Failure Containment Rules

- a slow or broken client must not impact other clients
- a file watcher fault must not terminate active sessions
- a provider process crash should transition only its own session to `Exited` or `Error`
- persistence failures should degrade recovery guarantees, not break live session execution
