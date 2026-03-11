# MVP Checklist

This checklist defines the minimum end-to-end user flows that should work before calling the host daemon implementation an MVP.

The product should now be interpreted as a supervision-oriented session runtime. Terminal attach remains important, but the MVP should increasingly validate observe/intervene/watch workflows rather than terminal transport alone.

## Core User Flows

### 1. Start Local Session

Acceptance:

- daemon starts successfully
- a local terminal can create a daemon-managed session
- the local terminal attaches as the initial host controller
- provider output is visible and interactive locally

### 2. Discover Sessions Remotely

Acceptance:

- a remote client can fetch the active session list
- session metadata includes id, title, provider, status, and controller state
- a remote client can choose a session and attach without restarting the daemon

### 3. Observe Live Terminal Output

Acceptance:

- a remote client receives attach-time replay of recent terminal output
- a remote client continues receiving incremental terminal output
- terminal transport is binary-safe and does not fail on non-UTF-8 PTY bytes

### 4. Remote Control Handoff

Acceptance:

- a remote client can request control
- the host becomes a follower view while remote control is active
- remote input and resize drive the session PTY
- the host does not spuriously steal control back

### 5. Return Control to Host

Acceptance:

- a remote client can release control
- the host reclaims active control without restarting the session
- host input works again after reclaim
- resizing back to the host terminal restores a coherent local layout

### 6. Clean Session Termination

Acceptance:

- provider self-exit transitions the session to `Exited` or `Error`
- explicit stop via API/client terminates the session cleanly
- attached clients receive session lifecycle updates and exit notification

### 7. Runtime Observability

Acceptance:

- a session exposes basic runtime signals beyond raw terminal transport
- clients can see recent activity state clearly enough to distinguish active, waiting, idle, and ended sessions
- session summaries remain usable even when no terminal is currently attached

### 8. Truthful Session Inventory

Acceptance:

- session list and overview subscription stay accurate without manual refresh
- attached-client counts and controller state are operationally trustworthy
- `lastActivity` and `lastStateChange` reflect meaningful changes rather than internal polling noise
- inactive or ended sessions can be cleared without losing active supervision state

### 9. Minimum Supervision Signals

Acceptance:

- the architecture exposes a place for future `SessionPhase`
- clients receive at least coarse attention-oriented state without parsing raw PTY output themselves
- waiting-for-input and ended-session conditions are easy to surface in clients

### 10. File and Git Inspection

Acceptance:

- recent file changes are backed by real file-watcher data
- session summaries can surface coarse file/git hints
- clients can inspect changed files and git state in read-only mode
- file inspection remains safely scoped to the session workspace root

### 11. Lightweight Recovery

Acceptance:

- after daemon restart, session metadata records are available again
- recent terminal tail for persisted records is still accessible
- the system does not pretend that pre-restart live PTY processes survived

### 12. Minimum Access Control

Acceptance:

- a minimal pairing/auth mechanism exists for remote access
- a local host web UI can approve pairing requests
- a paired client can reconnect with its issued token
- unpaired or unauthorized clients cannot control sessions
- controller actions remain single-owner at a time

## Explicit Non-Goals for MVP

- full IDE-like file editing
- arbitrary attach to unrelated local terminals
- polished native mobile clients
- advanced multi-controller arbitration beyond one controller plus observers
- rich git authoring or file editing workflows
- deep provider-specific `SessionPhase` tuning
- full timeline/history analytics

## Recommended Next Build Order

1. implement real file watching and expose truthful recent-file session state
2. tighten session inventory truthfulness and overview subscriptions
3. add conservative attention/watch signals on top of `SessionSignals`
4. expose read-only file/git inspection cleanly in host and remote web clients
5. keep the web clients thin until runtime/event shape stabilizes
