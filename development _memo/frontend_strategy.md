# Frontend Strategy V1

This document freezes the current frontend direction for the host admin UI and the remote client.

## Decision

Keep the host admin UI in the runtime repository.

Move the maintained remote client into a separate repository:

- `~/dev/VibeEverywhere-Client`

Use the new React + Vite client repository as the maintained remote client implementation.

## Why

- the runtime is now stable enough that frontend structure matters
- the host admin UI and remote client are now diverging in purpose
- the remote client UI has been redesigned independently and should iterate faster than the runtime
- the separate client repository should focus on remote workflow, discovery, and grouping
- the runtime repository should stay focused on truthful host behavior and host-local admin

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

### 2. Remote Client

Audience:

- LAN clients on desktop/tablet/phone

Purpose:

- device discovery
- pairing request
- session inventory monitoring
- connect to one or more sessions
- observe terminal output
- request/release control
- read-only file inspection
- session grouping views across hosts

This is a session watch/intervene client, not a host configuration UI.

Multi-session handling should use in-page tabs, not browser tabs, as the primary model.

## Repo Layout Recommendation

Runtime repository:

- host admin UI
- daemon-served smoke pages while needed
- runtime API docs and contracts

Client repository:

- `~/dev/VibeEverywhere-Client`
- React + Vite application
- remote client UX only

Recommended near-term path:

1. keep current daemon-served remote smoke client working as reference
2. port runtime API into the separate client repo
3. add discovery and grouping support
4. retire older in-repo remote frontend only after the separate client reaches parity

## Shared UI Principles

- supervision first, terminal second
- inventory should update via subscription, not manual refresh
- session cards should emphasize truthful status, controller, and recent activity
- attention should be first-class in both sorting and badge hierarchy
- file/git surfaces stay read-only
- avoid IDE-like affordances
- keep the remote client mobile-first and compact
- host admin can stay desktop/localhost oriented
- remote client should merge sessions across paired hosts without making the host own cross-host group state

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

- Host admin in the runtime repo may continue with its current in-repo implementation.

- React + Vite client repo should own:
  - view composition
  - paired-device collections
  - discovered-device collections
  - session collections and sort/filter state
  - in-page session tabs
  - grouping views and group-tag editing
  - pairing and remote connection flows

- The daemon remains responsible for:
  - auth
  - pairing
  - discovery advertisements
  - session lifecycle
  - attention inference
  - terminal transport
  - session group-tag persistence

- Keep client-specific state thin over runtime truth. Do not duplicate backend semantics in the client.

## Deferred

- SSR
- PWA/offline work
- native mobile wrappers
- heavy global state libraries unless Angular signals/services become insufficient
