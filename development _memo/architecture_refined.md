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

There is one PTY per session. The PTY belongs to the session runtime, not to any specific UI surface.

The host-side terminal should eventually attach through the same daemon-managed session protocol as any remote client, usually as the initial controller.

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
- coordinate SessionRuntime, SnapshotStore, FileWatcher, and GitInspector
- manage return-of-control behavior when the active controller yields or disconnects

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

Do not persist:

- active PTY handles
- full process state
- full terminal history

## Failure Containment Rules

- a slow or broken client must not impact other clients
- a file watcher fault must not terminate active sessions
- a provider process crash should transition only its own session to `Exited` or `Error`
- persistence failures should degrade recovery guarantees, not break live session execution

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
