# Client / Runtime Parallel Plan

## Goal

Parallelize the next milestone without letting frontend work block on backend churn.

The next milestone has three major deliverables:

1. separate remote client integration in `~/dev/VibeEverywhere-Client`
2. UDP discovery
3. session group tags and grouping views

## Freeze These Interfaces First

These interfaces should be treated as the short-term contract.

### Existing Session Read Model

The client can already depend on:

- `GET /host/info`
- `POST /pairing/request`
- `POST /pairing/claim`
- `GET /sessions`
- `POST /sessions`
- `GET /sessions/{sessionId}/snapshot`
- `GET /sessions/{sessionId}/file`
- `GET /sessions/{sessionId}/tail`
- `GET /ws/overview`
- `GET /ws/sessions/{sessionId}`

### New Discovery Read Model

Add:

- UDP discovery payload
- `GET /discovery/info`

Freeze payload fields as:

- `hostId`
- `displayName`
- `remoteHost`
- `remotePort`
- `protocolVersion`
- `tls`

### New Session Group Tag Model

Freeze session tag representation as:

- `groupTags: string[]`

Rules:

- a session may have multiple tags
- tags are stored per session by the host
- the client uses tags for grouping, filtering, and alternate inventory views
- the host does not maintain a cross-session group object or membership graph

Add:

- include `groupTags` in session summary and snapshot payloads
- `POST /sessions/{sessionId}/groups`

Suggested request:

```json
{
  "mode": "add",
  "tags": ["frontend"]
}
```

## Recommended Parallel Tracks

### Track A: Client API Integration

Repo:

- `~/dev/VibeEverywhere-Client`

Scope:

- replace mock auth/inventory/explorer data with real runtime REST + WebSocket flows
- preserve current UI structure
- no UDP discovery yet
- no grouping logic beyond displaying `groupTags` if present

Blocked by:

- nothing, if it targets the already-existing runtime API first

### Track B: Discovery Runtime

Repo:

- runtime repo

Scope:

- define discovery payload
- add UDP broadcast sender on host
- add `GET /discovery/info`
- document discovery cadence, TTL, and opt-in config

Blocked by:

- no client dependency for initial implementation

### Track C: Discovery Client

Repo:

- `~/dev/VibeEverywhere-Client`

Scope:

- browser-side discovery UX and saved-device state in `AuthSection`
- merge manually configured hosts, verified hosts, and paired hosts cleanly
- do not assume raw UDP receive is possible in the browser
- keep `GET /discovery/info` verification as the current web-client discovery baseline

Blocked by:

- browser runtime constraints if the goal is true automatic discovery

### Track D: Session Group Tags Runtime

Repo:

- runtime repo

Scope:

- add `groupTags` to in-memory session model
- persist `groupTags`
- expose tags in `/sessions`, `/sessions/{id}`, `/snapshot`, and WS updates
- add `POST /sessions/{id}/groups`

Blocked by:

- no client dependency for initial implementation

### Track E: Grouping View Client

Repo:

- `~/dev/VibeEverywhere-Client`

Scope:

- map existing grouping UI to real session tags
- allow filtering and grouping across paired hosts by tag
- add add/remove tag interactions

Blocked by:

- Track D contract

### Track F: Client Docs And Repo Cleanup

Repo:

- `~/dev/VibeEverywhere-Client`

Scope:

- replace AI Studio README and app language
- rename placeholder concepts
- document local dev against `vibe-hostd`

Blocked by:

- none

## Suggested Execution Order

1. Track F immediately
2. Track A immediately
3. Track B immediately
4. Track D immediately
5. Track C once discovery payload is frozen
6. Track E once group tag payload is frozen

This allows four tracks to start in parallel with minimal conflict.

## Conflict Boundaries

To reduce merge pain:

- Track A owns client API service layer and state stores
- Track C owns discovery-specific client state and `AuthSection`
- Track E owns grouping interactions and inventory/explorer grouping views
- Track B owns discovery runtime modules and docs
- Track D owns session-tag runtime model, storage, and API
- Track F owns `README.md` and terminology cleanup in the client repo

Avoid overlapping edits in the same client files unless the interfaces are already merged.

## Acceptance Criteria

### Milestone 1

- new client can pair and list sessions from one host
- new client can open a live session and render terminal output
- client README explains the real runtime flow

### Milestone 2

- native/mobile client:
  - nearby hosts appear automatically via UDP discovery
  - discovered hosts can be paired without manual address entry
- web client:
  - manual host add + verify remains the baseline unless a helper/relay is introduced

### Milestone 3

- sessions expose `groupTags`
- client can filter and organize sessions by group across hosts
- tag edits round-trip to the host
