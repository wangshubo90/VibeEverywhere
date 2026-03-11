# iOS Client MVP

This document defines the first useful iOS client milestone.

The goal is not a polished App Store app. The goal is a reliable internal client that can drive real smoke testing away from the desk.

## Product Goal

The iPhone user can:

1. connect to a host on the LAN
2. pair once
3. see current sessions
4. attach to a session
5. observe terminal output
6. request control
7. send input and resize
8. release control or stop the session

## Explicit Non-Goals For iOS MVP

- full file tree or file editing
- rich git view
- multiple simultaneous host profiles with discovery
- iPad-specific multiwindow polish
- offline caching of large terminal history
- a perfect mobile terminal keyboard experience

## Screens

### 1. Host Connect Screen

Fields:

- host/IP
- port
- saved hosts

Actions:

- check reachability
- open pairing flow
- connect with saved token

### 2. Pairing Screen

Shows:

- pairing code
- pairing status
- approval instructions for host user

Actions:

- start pairing
- cancel
- save approved token

### 3. Sessions Screen

Shows:

- host display name
- session list
- provider
- title
- status
- controller state

Actions:

- refresh
- create session
- attach to session

### 4. Session Screen

Shows:

- terminal view
- connection state
- controller state
- session status

Actions:

- request control
- release control
- stop session
- reconnect

## MVP Acceptance

### A. Host Connection

- user can enter a host and port
- app can reach `GET /health`
- app can fetch `GET /host/info`

### B. Pairing

- app can start pairing
- app can survive waiting for host approval
- app stores token after approval
- app can reconnect later without re-pairing every time

### C. Session Discovery

- app can list sessions
- app can show meaningful metadata
- app can attach to an already running session

### D. Terminal Observe

- app receives attach replay
- app receives incremental output
- binary-safe terminal data is decoded correctly

### E. Control

- app can request control
- observer state is visually distinct from controller state
- input is only sent when controller state is active
- release works cleanly

### F. Exit And Stop

- app reflects `session.exited`
- app can explicitly stop a session
- app handles disconnected sockets without corrupting local state

## Suggested Build Order

1. connection + host info
2. pairing + token storage
3. session list
4. session attach and terminal observe
5. control request/release
6. input + resize
7. stop + reconnect handling

## UX Constraints

The app should be plain and utilitarian.

Priorities:

- connection clarity
- control clarity
- reliable terminal behavior
- obvious recovery when disconnected

Do not spend time on heavy branding or visual polish before these are solid.

## Data Storage On Device

Store in Keychain or secure local storage:

- bearer token

Store in lightweight app storage:

- last host/IP
- last port
- known hosts list
- last attached session id if useful

## Risks

### Terminal Rendering

The server sends PTY bytes, not pre-rendered UI.

The iOS client will likely need:

- ANSI-aware rendering
- a simple terminal buffer/view model

This is the hardest client-side problem in MVP.

### Mobile Input

Terminal-like input on iPhone is awkward.

For MVP, correctness is more important than elegance.

### Control Handoff

One PTY means one controller.

The UI must make observer vs controller state explicit.

## Recommended Boundary With Server Work

The iOS client should rely only on:

- the REST endpoints in `client_api_ios.md`
- the WebSocket event/command contract in `client_api_ios.md`

It should not depend on host admin routes.
