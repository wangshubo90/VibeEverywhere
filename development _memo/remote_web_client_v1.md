# Remote Web Client V1

This document freezes the first real remote web client design.

## Scope

The remote web client is the supervision and intervention surface for non-host devices.

It is intentionally not the host admin UI.

## Primary Goals

1. Pair and reconnect easily.
2. See live session inventory without refreshing.
3. Inspect a session before attaching.
4. Observe terminal output cleanly.
5. Request/release control clearly.
6. Inspect changed files in read-only mode.

## Main User Flow

1. connect to host
2. pair or reuse token
3. watch the session inventory
4. select a session
5. inspect summary/file hints
6. connect to the session
7. request control if intervention is needed

## Layout

Desktop:

- left rail: host/session inventory
- main pane: selected session detail
- lower or side pane: logs / connection status

Phone:

- top: host/pairing/session header
- body: selected session view
- bottom sheet or collapsible panel: session list

## Session Inventory

Use grouped cards similar to the host UI, but slimmer.

Groups:

- Active
- Quiet
- Stopped / Recovered

Each card should show:

- title
- session id
- provider
- supervision state
- runtime status
- controller state
- attached client count if available
- last output
- last activity
- recent file change count
- git branch/dirty hint

Primary action:

- `Open`

## Session Detail View

Sections:

### Header

- title
- session id
- provider
- status
- supervision state
- controller state

### Control Bar

- `Connect`
- `Disconnect`
- `Request Control`
- `Release Control`
- `Stop`

### Terminal Panel

- xterm-style rendering
- observer/controller state visible near the terminal
- reconnect and exited states clearly shown

### Inspection Panel

- recent files
- read-only file content
- git summary

This should stay secondary to session supervision, not become a file browser.

## Pairing UX

The remote client should support:

- request pairing
- wait for host approval
- automatic token pickup/claim
- saved token reuse

Do not require manual token copy/paste in the normal flow.

## Visual Guidance

- optimize for quick scanning and low confusion
- keep the control path visible without scrolling around the page
- surface observer vs controller state clearly
- avoid noisy debug logs dominating the layout

## Implementation Guidance

- plain HTML/CSS/JS for v1
- move toward `web/remote_client/`
- keep `xterm.js` for terminal rendering
- use websocket inventory subscription for the session list
- use per-session websocket only when session detail is open

## Non-Goals

- host config
- pairing approval
- session editing
- workspace tree explorer
- diff/patch workflows
- polished mobile-native UX
