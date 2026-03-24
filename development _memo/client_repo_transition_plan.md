# Client Repo Transition Plan

## Purpose

The remote client is moving out of the runtime repository into:

- `~/dev/VibeEverywhere-Client`

The runtime repository continues to own:

- `vibe-hostd`
- pairing and authorization
- session lifecycle
- REST and WebSocket APIs
- host admin UI
- session persistence and supervision signals

The separate client repository owns:

- remote client UX
- device discovery UX
- multi-device inventory
- in-page terminal/session workspace
- grouping views across hosts

This keeps the runtime focused on truthful host behavior while allowing faster iteration on the client.

## Current State Of The New Client UI

The new client repository is a React + Vite app with a strong visual direction but no real runtime integration yet.

Current main surfaces:

- `AuthSection`
  - currently framed as node discovery / provisioning
  - good destination for device discovery, pairing, and trusted-device state
- `InventorySection`
  - currently framed as cluster inventory
  - should become session inventory across paired hosts
- `ExplorerSection`
  - currently framed as session explorer with terminal windows and grouping controls
  - should become the primary multi-session workspace with in-page tabs

Current language in the client repo still uses placeholder concepts:

- node clusters
- kinetic agents
- global execution
- infrastructure telemetry

This should be mapped to Vibe concepts:

- device
- host
- paired host
- session
- group tag
- live terminal attachment

## Contract Boundary

The client must not infer backend truth from raw PTY bytes.

The runtime remains the source of truth for:

- lifecycle status
- attention state and reason
- controller state
- recent file change counts
- git summary
- attachment counts

The client may derive only presentation state:

- sorting
- grouping view
- session tab arrangement
- selected device/session
- persisted local filters

## Immediate Integration Targets

The first port of the new client should use existing runtime APIs:

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

The first client port does not require new provider semantics.

## New Backend Additions Required

Two new backend capabilities are required for the new client direction.

### 1. UDP Device Discovery

Remote clients should be able to discover nearby hosts without manual entry.

Minimum runtime capability:

- host advertises a small discovery payload over UDP broadcast
- client listens and builds a discovered-device list
- payload includes enough metadata to start pairing and fetch host info

Suggested payload fields:

- `hostId`
- `displayName`
- `remoteHost`
- `remotePort`
- `protocolVersion`
- `tlsEnabled`

This should remain advisory. Pairing and authorization still gate real access.

### 2. Session Group Tags

Sessions need lightweight group tags that are host-owned per session. Each session may have multiple tags.

The client does not own a server-side group object. It only uses tags to build grouping and filtering views across hosts.

Runtime responsibility:

- store zero or more group tags on each session
- expose tags in session summaries and snapshots
- allow add/remove/set tag operations through API

Client responsibility:

- define grouping views from tags
- merge sessions across hosts by shared tags
- render grouped inventory/workspace

The host does not need a global group membership graph.

## Frontend Mapping

### AuthSection

Map to:

- discovered hosts
- pairing request
- pairing claim
- saved paired hosts
- manual host entry fallback

### InventorySection

Map to:

- sessions across paired hosts
- compact cards or rows
- sort and filter controls
- group tag visibility
- group tag editing affordance

### ExplorerSection

Map to:

- in-page session tabs
- terminal attachments
- overview and file inspection
- grouping-focused navigation

## Recommended Implementation Order

1. Port existing runtime API into the new client without discovery or grouping first.
2. Add UDP discovery and device list behavior.
3. Add session group tags on the backend.
4. Wire client grouping views on top of the new tags.

This keeps the first usable client milestone close to current working runtime behavior.

## Non-Goals For The First Port

- provider-specific semantic parsing
- session editing workflows
- cross-host coordinated session orchestration
- server-side group membership management
- replacing host admin UI
