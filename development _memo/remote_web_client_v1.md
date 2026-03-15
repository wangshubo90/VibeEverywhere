# Remote Web Client V1

This document freezes the first real remote web client design.

## Scope

The remote web client is the supervision and intervention surface for non-host devices.

It is intentionally not the host admin UI.

## Primary Goals

1. Pair and reconnect easily.
2. See live session inventory without refreshing.
3. Inspect a session before attaching.
4. Connect to multiple sessions without leaving the page.
5. Observe terminal output cleanly.
6. Request/release control clearly.
7. Inspect changed files in read-only mode.

## Main User Flow

1. connect to host
2. pair or reuse token
3. watch the session inventory
4. select a session
5. inspect summary/file hints
6. open the session in an in-page connection tab
7. connect to the session
8. request control if intervention is needed

## Layout

Desktop:

- left rail: session inventory and sort/filter controls
- top tab bar: open session connections
- main pane: selected session detail + terminal
- lower or side pane: logs / connection status

Phone:

- top: host/pairing/session header
- below header: compact session tab strip
- body: selected session view
- collapsible list panel: session inventory and sort mode

## Session Inventory

Use grouped cards similar to the host UI, but slimmer.

Groups:

- Live
- Ended
- Archived Records

Each card should show:

- title
- session id
- provider
- runtime status
- live / ended / archived label
- controller state
- conversation id if present
- attached client count if available
- last output age
- last activity age
- recent file change count
- git branch/dirty hint
- attention hint

Primary action:

- `Open`

Inventory must support user-selected sorting.

Minimum sort modes:

- attention
- recent activity
- recent output
- created time
- title
- provider

Default sort should be attention-first.

## Session Detail View

Sections:

### Header

- title
- session id
- provider
- status
- live / ended / archived label
- controller state
- conversation id if present

### Control Bar

- `Connect`
- `Disconnect`
- `Request Control`
- `Release Control`
- `Stop`

### Connection Tabs

- each opened session gets an in-page tab
- each tab may be connected or disconnected independently
- each tab is closable without affecting the session itself
- tab titles should remain compact on phone screens

### Terminal Panel

- xterm-style rendering
- observer/controller state visible near the terminal
- reconnect and exited states clearly shown

### Inspection Panel

- recent files
- read-only file content
- git summary
- attention summary: needs input, remote control active, workspace changed, ended, archived

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
- make attention state readable without relying on the raw event log
- avoid noisy debug logs dominating the layout
- keep per-session cards compact enough for vertical mobile browsing

## Implementation Guidance

- Angular app inside the shared frontend workspace
- keep `xterm.js` for terminal rendering
- use websocket inventory subscription for the session list
- use per-session websocket only for opened session tabs
- treat in-page tab state as UI state, not runtime truth

## Non-Goals

- host config
- pairing approval
- session editing
- workspace tree explorer
- diff/patch workflows
- browser-tab-based multi-session UX
