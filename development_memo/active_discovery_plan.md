# Active Discovery

This document records the current discovery shape in the product.

It is no longer a speculative plan. It should describe what exists and where the remaining boundaries are.

## Current Runtime Truth

The runtime currently supports:

- UDP discovery broadcast on port `18087`
- `GET /discovery/info`
- stable `hostId` in discovery and host-info payloads
- advertised remote host/port and TLS flags

Relevant implementation:

- `src/net/discovery_broadcaster.cpp`
- `src/net/http_shared.cpp`
- `src/net/json.cpp`

## Current Client Consumption

### iOS

The maintained iOS client can consume discovery natively.

Client repo:

- https://github.com/shubow-sentrits/Sentrits-IOS

### Web

The maintained browser client cannot consume UDP directly.

Current browser path:

- a local discovery helper listens for UDP
- the helper exposes an HTTP feed the browser can poll
- the browser verifies hosts and persists trusted/manual/paired records separately

Client repo:

- https://github.com/shubow-sentrits/Sentrits-Web

## Discovery Identity Rules

Current identity rules:

- `hostId` is the canonical host identity
- `displayName` is descriptive only
- wildcard bind addresses must not become the client-facing connect address
- clients should dedupe by `hostId` whenever present

## Browser Boundary

Important architecture boundary:

- browsers do not receive raw UDP discovery directly
- browser discovery therefore depends on a helper bridge or on explicit known-host flows

That is a real platform boundary, not a missing frontend polish item.

## What Discovery Is For

Discovery is used to:

- find hosts on the local network
- verify them through `GET /discovery/info`
- start or simplify pairing flows

Discovery is not itself authorization.

Pairing and bearer-token authorization still gate real session access.

## Packaging Implication

For daemon-first packaging, browser discovery helper behavior should be treated as packaging/runtime integration work rather than as a browser-only feature.

See:

- `packaging_architecture.md`
