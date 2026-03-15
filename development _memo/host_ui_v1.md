# Host Admin UI V1

This document freezes the first real host-admin UI design.

## Scope

The host admin UI is localhost-only.

It is the control room for:

- pairing approval
- trusted device management
- host config
- session creation
- session inventory
- attached-client management
- session cleanup
- read-only inspection shortcuts

It should not act like the remote client.

## Primary Goals

1. Show what sessions exist right now.
2. Show who is attached to them.
3. Make it obvious which session needs operator action.
4. Let the host create, stop, clear, and inspect sessions quickly.
5. Let the host remove trusted devices and expire access without leaving the page.
6. Expose host config and pairing without leaving the page.

## Information Architecture

Use a two-column layout on desktop and a stacked layout on narrow screens.

### Left Column

- host status
- host config
- pending pairing approvals
- trusted devices
- attached clients summary

### Right Column

- session inventory
- file inspector panel
- event/log panel

## Session Inventory Design

Group sessions into sections:

- Live
- Ended
- Archived Records

Each session card should show:

- title
- session id
- provider
- runtime status
- live / ended / archived label
- workspace root
- conversation id if present
- controller state
- attached client count
- created time
- last state change
- last output
- last activity
- recent file change count
- git dirty/branch summary

Card actions:

- `Create Session` available from the session area, not only CLI
- `Files`
- `Stop`
- `Clear Ended/Archived` for inventory cleanup without affecting live sessions
- `Copy CLI Command` or `Copy Session Id` for native-terminal attach/resume help

Do not embed a browser terminal attach flow in the host admin UI.

## Attached Client View

Show a compact client list with:

- client id
- device label if known
- session id
- role: observer/controller/host
- connected time if available

Action:

- `Disconnect`

## Trusted Devices View

Show a compact trusted-device list with:

- device name
- device type
- device id
- token/approval age
- last seen time if available

Actions:

- `Remove Device`
- `Expire Token`

## Pairing Area

Show pending requests in a compact approval queue.

Each item should show:

- device name
- device type
- short code
- created time
- time-to-expiry if available

Action:

- `Approve`
- optional future `Reject`

## Host Config Area

Fields for v1:

- admin host
- admin port
- remote host
- remote port
- provider command override: codex
- provider command override: claude
- remote TLS certificate reference/status

Actions:

- `Save`
- `Download Remote TLS Certificate`

## Session Creation

Host admin must support session creation directly.

Minimum fields:

- provider
- title
- workspace root
- optional conversation id
- optional explicit command override

Created sessions should appear immediately in the session list.

## Visual Guidance

- keep it operational and dense, not decorative
- use strong grouping and status badges
- make live vs ended vs archived visually obvious
- avoid calling ended sessions "stopped" in the inventory
- keep destructive actions visually clear but not noisy
- optimize for localhost desktop use, not phone use

## Implementation Guidance

- Angular app inside the shared frontend workspace
- subscribe to host-wide session inventory via websocket
- refresh client presence in response to inventory changes
- avoid overcoupling UI layout to raw backend field names
- keep browser terminal code out of this app

## Non-Goals

- embedded full terminal dashboard
- remote attach experience parity
- workspace tree browser
- editing or git write actions
- mobile-first layout work
