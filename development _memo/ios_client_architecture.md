# iOS Client Architecture Notes

This is a practical Swift-first architecture note for the initial client.

## Language And Platform

- Swift
- iOS first
- SwiftUI for app UI

Recommended minimum split:

- UI in SwiftUI
- network and protocol code in plain Swift types
- terminal rendering isolated behind a dedicated module or subsystem

## Suggested Modules

### `HostClient`

Responsible for:

- REST calls
- auth header injection
- token persistence handoff

Possible responsibilities:

- `fetchHostInfo()`
- `startPairing()`
- `listSessions()`
- `createSession()`
- `fetchSnapshot()`
- `stopSession()`

### `SessionSocket`

Responsible for:

- WebSocket connection
- event decoding
- command sending
- reconnect coordination

Suggested outputs:

- async stream of server events
- explicit connection state

### `TerminalEngine`

Responsible for:

- base64 decode
- byte append
- ANSI parsing/render model

This should be isolated early because it is the part most likely to change.

### `TokenStore`

Responsible for:

- Keychain bearer token storage
- retrieving token for host/session calls

### `AppState`

Responsible for:

- selected host
- current session list
- active session screen state
- current controller state

## Suggested Swift Concurrency Style

Prefer:

- `async/await` for REST
- `AsyncStream` or actor-backed event delivery for WebSocket events
- actors for shared mutable connection state when useful

Avoid:

- mixing callback-heavy networking with ad hoc global mutable state

## JSON Strategy

Define narrow `Codable` models for:

- pairing request/response
- host info
- session summary
- session snapshot
- WebSocket events
- WebSocket commands

For WebSocket events:

- decode the `type` discriminator first
- then decode into typed payloads

Be tolerant of additive server fields.

## Terminal Strategy

There are two possible approaches.

### Option A: Build A Small Native Terminal View Model

Pros:

- full control
- pure Swift

Cons:

- more work
- ANSI rendering complexity

### Option B: Wrap An Existing Terminal/ANSI Renderer

Pros:

- faster path if a suitable Swift/UIKit component exists

Cons:

- dependency risk
- may still require adaptation for PTY-like behavior

Recommendation:

- evaluate existing Swift/iOS terminal rendering options first
- if none is good enough, build a minimal terminal buffer that handles the subset needed for Codex/Claude CLI smoke testing

## Initial Screen View Models

### `ConnectViewModel`

- host
- port
- connectivity state
- last error

### `PairingViewModel`

- pairing request
- waiting/approved/expired state
- token save result

### `SessionsViewModel`

- session list
- load state
- create-session form state

### `SessionViewModel`

- session summary
- websocket state
- control state
- terminal content source
- last protocol error

## Logging

Add lightweight structured client logging early for:

- REST failures
- WebSocket close/error
- decode failures
- control handoff transitions

This will save time during smoke testing.

## Testing Guidance

Before UI-heavy tests, cover:

- REST decoding
- WebSocket event decoding
- control state transitions
- token persistence behavior
- terminal byte ingestion behavior

Use fake event streams before relying on live daemon testing for every client change.
