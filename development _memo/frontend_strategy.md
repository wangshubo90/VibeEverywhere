# Frontend Strategy V1

This document freezes the first frontend direction for the host admin UI and the remote web client.

## Decision

Keep both frontend surfaces in the current repository for now.

Do not introduce a full frontend framework yet.

## Why

- the runtime/event model is still moving
- the UI surfaces are still operational tools, not polished product apps
- a framework split would add packaging and coordination overhead before the backend contracts are stable
- static assets served by the daemon are still sufficient for the current MVP

## Frontend Surfaces

There are two distinct web surfaces and they should stay conceptually separate.

### 1. Host Admin UI

Audience:

- the host machine owner
- localhost only

Purpose:

- pairing approval
- host configuration
- session supervision and cleanup
- attached-client visibility and control

This is an operational console, not a terminal-first interface.

### 2. Remote Web Client

Audience:

- LAN clients on desktop/tablet/phone

Purpose:

- pairing request
- session inventory monitoring
- attach to a session
- observe terminal output
- request/release control
- read-only file inspection

This is a session watch/intervene client, not a host configuration UI.

## Repo Layout Recommendation

Keep assets in-repo, but split them into explicit app folders:

- `web/host_ui/`
- `web/remote_client/`

Do not keep growing `tests_smoke/` as the long-term home of the remote UI.

Recommended near-term path:

1. keep `tests_smoke/` working until parity exists
2. move the remote client into `web/remote_client/`
3. keep both apps framework-free for v1

## Shared UI Principles

- supervision first, terminal second
- inventory should update via subscription, not manual refresh
- session cards should emphasize truthful status, controller, and recent activity
- file/git surfaces stay read-only
- avoid IDE-like affordances
- keep layouts usable on laptop and phone

## Data Model Expectations

Both frontends should rely on:

- REST for initial load and explicit actions
- WebSocket inventory subscription for live session-state updates
- per-session WebSocket for terminal and per-session live updates

Neither frontend should re-infer high-level session meaning from terminal bytes.

## Framework Decision Revisit

Revisit a framework only when one of these becomes true:

- state coordination across screens becomes awkward in plain JS
- the remote client grows beyond operational session monitoring
- terminal, inventory, and detail panes need reusable component structure
- design work starts to dominate over runtime API changes

Until then, keep the implementation simple and close to the served daemon assets.
