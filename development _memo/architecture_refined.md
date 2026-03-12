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

The host is the owner of session execution and observation. Clients are subscribers, watchers, and optional controllers.

There is one PTY per session. The PTY belongs to the session runtime, not to any specific UI surface.

The host-side terminal should eventually attach through the same daemon-managed session protocol as any remote client, usually as the initial controller.

The terminal path is important, but it is not the whole product. The daemon is a supervision-oriented session runtime and control plane, not merely a PTY forwarder.

## Primary Runtime Components

### API Layer

Responsibilities:

- expose REST routes
- authenticate requests
- expose a local host web app for approval and configuration
- manage WebSocket session subscriptions
- translate between wire schema and internal service calls

### SessionManager

Responsibilities:

- create, load, list, stop, and inspect sessions
- own session registry
- arbitrate controller assignment
- coordinate SessionRuntime, SnapshotStore, FileWatcher, GitInspector, and future ProcessInspector / ResourceMonitor components
- manage return-of-control behavior when the active controller yields or disconnects
- aggregate low-level runtime signals into session-observable state
- keep session inventory fields truthful enough for host and remote supervision views

### SessionInference

Responsibilities:

- consume PTY, filesystem, process, and resource signals
- infer higher-level supervisory state
- derive attention state separately from process lifecycle
- expose `attentionState`, `attentionReason`, and later `attentionSince`
- expose a coarse `SessionPhase` only as a future seam
- remain provider-agnostic by default
- avoid treating raw terminal activity alone as the whole product signal

`SessionPhase` should be supported by the architecture now, but not overfit too early. The first implementation should keep the phase model coarse and tunable because some detection logic may vary by provider and prompt style.

Attention should be the first production supervisory layer before deep phase inference. It should be conservative, time-aware, and based primarily on structured signals such as lifecycle, output/activity timestamps, file changes, git transitions, and controller changes.

### SessionRuntime

Responsibilities:

- manage a single provider process through PTY
- capture raw output
- append to SessionOutputBuffer
- accept terminal input and resize requests
- report lifecycle changes
- maintain the currently active PTY size

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
- broadcast controller changes to observers
- support both per-session streams and host-wide inventory subscriptions

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
- maintain a recent changed-file set suitable for session summaries and snapshots
- feed both supervision signals and read-only client inspection surfaces

### ProcessInspector

Responsibilities:

- observe child-process tree shape
- summarize spawned commands
- provide signals for long-running task detection

### ResourceMonitor

Responsibilities:

- sample CPU and memory usage
- provide optional resource-alert signals

### GitInspector

Responsibilities:

- sample repository status periodically or on demand
- emit summarized git state
- provide stable dirty/clean and file-count semantics for session inventory views

### AuthManager

Responsibilities:

- device pairing
- token validation
- authorization policy checks
- host-local approval of pairing requests
- host identity and certificate management

### HostConfigStore

Responsibilities:

- persist host identity
- persist TLS certificate/key references or generated material
- persist daemon-local configuration needed by the host web UI

## Recommended Internal Flow

```text
Client
  -> API Layer
  -> SessionManager
  -> SessionRuntime / SessionInference / SnapshotStore / AuthManager

Provider Process
  -> PTY
  -> SessionRuntime
  -> SessionOutputBuffer
  -> SessionInference
  -> ClientDispatcher
  -> WebSocket subscribers

Workspace
  -> FileWatcher / GitInspector
  -> SessionInference / SessionManager
  -> API Layer / WebSocket subscribers

Process Tree / Resource Signals
  -> ProcessInspector / ResourceMonitor
  -> SessionInference
  -> API Layer / WebSocket subscribers

Pairing Client
  -> API Layer
  -> AuthManager
  -> HostConfigStore / PairingStore
  -> local host approval UI
```

## Threading Model

Recommended first implementation:

- one async event loop for network and timers
- one PTY reader task or thread per active session
- bounded queues or direct append path into `SessionOutputBuffer`
- dispatcher work scheduled independently from PTY reads

This gives a clean separation between high-priority PTY ingestion and comparatively lower-priority client delivery.

## Control Model

Recommended product rule:

- many observers may attach to a session
- one controller may mutate a session at a time
- controller owns terminal input and PTY resize
- if no controller is active, the session uses a configured default PTY size
- if a remote client takes control, host views follow the resulting byte stream
- host may explicitly reclaim control

This rule keeps PTY semantics correct because the provider process only ever sees one terminal size at a time.

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
- paired device records
- host identity and TLS material references
- lightweight signal summaries and last-observed supervisory state when useful for recovery

Do not persist:

- active PTY handles
- full process state
- full terminal history
- overconfident inferred phase history in MVP

## Failure Containment Rules

- a slow or broken client must not impact other clients
- a file watcher fault must not terminate active sessions
- a provider process crash should transition only its own session to `Exited` or `Error`
- persistence failures should degrade recovery guarantees, not break live session execution

## Session State Layers

The runtime should keep two distinct state layers:

1. Low-level runtime state
- process status
- PTY connectivity
- controller ownership
- websocket/client attachment state

2. Supervisory state
- recent filesystem activity
- PTY output rate
- attention level and reason
- attention decay/cooldown timestamps
- child-process activity
- resource signals
- coarse `SessionPhase`
- attention flags such as waiting-input, idle-too-long, or long-task

The first supervision implementation should bias toward truthful, inspectable fields over ambitious inference. File-change counts, git-dirty transitions, attached-client counts, and meaningful activity timestamps are more important than a deep phase taxonomy in the near term.

Clients should not need to infer these from raw signals themselves.

## Web Product Direction

Near-term web work should stay intentionally light:

- local host admin UI remains a small operational interface
- remote web client remains a focused session client for observe/control/watch flows
- avoid introducing a full frontend framework until runtime state, event schema, and supervision workflows stabilize

This keeps product iteration centered on runtime correctness rather than frontend churn.

## MVP Security Model

Recommended MVP flow:

- daemon serves a local host web app for configuration and pairing approval
- remote clients know or enter a host address manually
- remote client requests pairing
- daemon shows a short pairing code in the local host UI
- local user approves the request after confirming the code
- daemon issues a long-lived paired-device token
- subsequent REST and WebSocket requests require that token
- transport should move to HTTPS/WSS with a host-generated self-signed certificate

This deliberately avoids depending on mDNS or a complex custom pairing protocol in the MVP.

## Frozen Parallelization Seams

To reduce merge conflicts, freeze these boundaries before parallel implementation:

### `vibe::auth`

Suggested stable responsibilities:

- pairing request lifecycle
- paired-device record lifecycle
- bearer-token validation
- authorization decisions

Suggested stable types:

- `DeviceId`
- `PairingRequest`
- `PairingRecord`
- `AuthResult`
- `PairingService`
- `Authorizer`

### `vibe::store`

Suggested stable responsibilities:

- host identity persistence
- pairing persistence
- persisted session metadata and recent tail persistence

Suggested stable types:

- `HostConfigStore`
- `PairingStore`
- `SessionStore`

### `vibe::service`

Freeze `SessionManager` as the owner of live session runtime state, but keep auth and persistence concerns outside it except through narrow interfaces.
