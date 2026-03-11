# Host Admin UI V1

This document freezes the first real host-admin UI design.

## Scope

The host admin UI is localhost-only.

It is the control room for:

- pairing approval
- host config
- session inventory
- attached-client management
- session cleanup
- read-only inspection shortcuts

It should not act like the remote client.

## Primary Goals

1. Show what sessions exist right now.
2. Show who is attached to them.
3. Make it obvious which session needs operator action.
4. Let the host stop sessions and disconnect clients quickly.
5. Expose host config and pairing without leaving the page.

## Information Architecture

Use a two-column layout on desktop and a stacked layout on narrow screens.

### Left Column

- host status
- host config
- pending pairing approvals
- attached clients summary

### Right Column

- session inventory
- file inspector panel
- event/log panel

## Session Inventory Design

Group sessions into sections:

- Active
- Quiet
- Stopped / Recovered

Each session card should show:

- title
- session id
- provider
- runtime status
- supervision state
- workspace root
- controller state
- attached client count
- created time
- last state change
- last output
- last activity
- recent file change count
- git dirty/branch summary

Card actions:

- `Attach`
- `Files`
- `Stop`
- later: `Clear` only for inactive sessions if needed

## Attached Client View

Show a compact client list with:

- client id
- device label if known
- session id
- role: observer/controller/host
- connected time if available

Action:

- `Disconnect`

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

## Visual Guidance

- keep it operational and dense, not decorative
- use strong grouping and status badges
- make active vs quiet vs stopped visually obvious
- keep destructive actions visually clear but not noisy

## Implementation Guidance

- plain HTML/CSS/JS
- static assets in `web/host_ui/`
- subscribe to host-wide session inventory via websocket
- refresh client presence in response to inventory changes
- avoid overcoupling UI layout to raw backend field names

## Non-Goals

- embedded full terminal dashboard
- remote attach experience parity
- workspace tree browser
- editing or git write actions
