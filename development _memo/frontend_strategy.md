# Frontend Strategy V1

This document freezes the first frontend direction for the host admin UI and the remote web client.

## Decision

Keep both frontend surfaces in the current repository for now.

Introduce a real frontend stack now.

Use Angular for the first maintained frontend implementation.

## Why

- the runtime is now stable enough that frontend structure matters
- both the host admin UI and remote client need stronger state coordination than the smoke pages
- in-page multi-session tabs and richer inventory/detail behavior are easier to maintain in a component model
- Angular gives a disciplined app structure for forms, stateful views, and route-level separation
- keeping both apps in the same repository still keeps backend/frontend coordination tight

## Frontend Surfaces

There are two distinct web surfaces and they should stay conceptually separate.

### 1. Host Admin UI

Audience:

- the host machine owner
- localhost only

Purpose:

- pairing approval
- trusted device management
- host configuration
- create session
- session supervision and cleanup
- attached-client visibility and control

This is an operational console, not a terminal-first interface.

The host browser UI should not act as a session terminal.

Native host interaction should happen through the daemon-aware CLI in a local terminal.

### 2. Remote Web Client

Audience:

- LAN clients on desktop/tablet/phone

Purpose:

- pairing request
- session inventory monitoring
- connect to one or more sessions
- observe terminal output
- request/release control
- read-only file inspection

This is a session watch/intervene client, not a host configuration UI.

Multi-session handling should use in-page tabs, not browser tabs, as the primary model.

## Repo Layout Recommendation

Keep frontend sources in-repo in an explicit Angular workspace:

- `frontend/`
  - `projects/host-admin/`
  - `projects/remote-client/`
  - `projects/shared-ui/`
  - `projects/session-model/`

Built assets should still be served by the daemon as static output.

Recommended near-term path:

1. keep current static pages working until Angular parity exists
2. create one Angular workspace with two apps
3. preserve distinct host-admin and remote-client outputs
4. retire the smoke pages only after feature parity

## Shared UI Principles

- supervision first, terminal second
- inventory should update via subscription, not manual refresh
- session cards should emphasize truthful status, controller, and recent activity
- attention should be first-class in both sorting and badge hierarchy
- file/git surfaces stay read-only
- avoid IDE-like affordances
- keep the remote client mobile-first and compact
- host admin can stay desktop/localhost oriented

## Frontend Design Process

For both surfaces, define in this order:

1. behavior and functionality
2. information architecture
3. layout and interaction design
4. implementation guidance

Do not start from mockups alone.

The main design artifact should describe:

- purpose
- user jobs
- required behaviors
- state and data expectations
- layout rules
- non-goals

## Data Model Expectations

Both frontends should rely on:

- REST for initial load and explicit actions
- WebSocket inventory subscription for live session-state updates
- per-session WebSocket for terminal and per-session live updates

Neither frontend should re-infer high-level session meaning from terminal bytes.

## Framework Notes

- Angular should own:
  - view composition
  - session collections and sort/filter state
  - in-page session tabs
  - forms and trusted-device management

- The daemon remains responsible for:
  - auth
  - pairing
  - session lifecycle
  - attention inference
  - terminal transport

- Keep Angular-specific state thin over runtime truth. Do not duplicate backend semantics in the client.

## Deferred

- SSR
- PWA/offline work
- native mobile wrappers
- heavy global state libraries unless Angular signals/services become insufficient
