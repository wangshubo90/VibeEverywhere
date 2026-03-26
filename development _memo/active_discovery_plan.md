# Active Discovery Plan

## Current Truth

The runtime host is ready to advertise itself:

- `GET /discovery/info` is live
- UDP broadcast is live on port `18087`
- discovery payload is already stable enough for clients:
  - `hostId`
  - `displayName`
  - `remoteHost`
  - `remotePort`
  - `protocolVersion`
  - `tls`

What is **not** ready is true client-side automatic discovery in the maintained web client.

## Constraint

The maintained client in `~/dev/VibeEverywhere-Client` is a browser app built on Vite/React.

Browsers do not expose raw UDP sockets for LAN broadcast receive. That means:

- the browser client cannot directly listen for host UDP discovery advertisements
- true automatic discovery is not implementable as a pure frontend change in the current web client

This is an architecture boundary, not just a missing feature.

## What "True Active Discovery" Means

Real active discovery means:

1. the host broadcasts discovery advertisements over UDP
2. the client receives those advertisements directly
3. the client builds a discovered-device list without manual host entry
4. the client optionally verifies a selected device via `GET /discovery/info`
5. the client starts pairing from that discovered device record

The runtime already implements step 1 and step 4.

## Feasible Client Paths

### Path A: Native Client Discovery

Best long-term path.

- iOS / Android / desktop native client listens on UDP port `18087`
- client stores discovery candidates locally
- user pairs directly from discovered devices

This is the correct solution for the future mobile app.

### Path B: Local Discovery Helper For Web Client

Pragmatic bridge if the web client must support discovery.

- run a tiny local helper process on the same machine as the browser
- helper listens for UDP discovery
- helper exposes a local HTTP/WebSocket endpoint the browser can read
- browser consumes helper output as a discovered-device feed

This keeps the runtime protocol unchanged, but introduces local app/helper complexity.

### Path C: Discovery Relay Through A Known Host

Useful as a fallback, but not true zero-entry discovery.

- browser already knows at least one reachable host
- that host can expose recently observed discovery advertisements
- browser lists those relayed devices

This is not first-contact discovery, but it may still be useful operationally.

## Recommended Direction

### For current web client

Do **not** pretend the browser can do raw UDP discovery.

Instead:

- keep manual host entry + `/discovery/info` verification as the current web baseline
- improve that UX if needed
- do not label it as true automatic discovery

### For the next real discovery milestone

Implement true active discovery in the native/mobile client first.

That is the cleanest and most honest path.

If browser discovery becomes important before the native client is ready, use Path B with a local helper.

## Suggested Parallel Split

### Track A: Runtime Discovery Hardening

Repo:

- runtime repo

Scope:

- confirm discovery payload stability
- document broadcast cadence / TTL expectations
- add any missing test coverage for discovery lifecycle

### Track B: Native Client Discovery Consumption

Repo:

- native/mobile client repo

Scope:

- UDP listener
- discovered-device store
- verify-and-pair flow from discovered devices

### Track C: Web Client Discovery UX Clarification

Repo:

- `~/dev/VibeEverywhere-Client`

Scope:

- relabel current flow as manual verify/add
- avoid implying raw LAN auto-discovery exists
- keep saved devices / pairing clean

### Track D: Optional Browser Helper Spike

Repo:

- separate helper repo or small utility module

Scope:

- local UDP listener
- local HTTP/WebSocket feed for browser consumption
- prove whether browser discovery is worth supporting

## Green-Light Criteria For True Discovery Smoke

Do not call discovery "ready to smoke" until all are true:

1. client receives advertisements without manual host entry
2. discovered device list is stable across repeated broadcasts
3. selecting a discovered device can verify and pair successfully
4. duplicate hosts are deduped by `hostId`
5. wildcard bind addresses do not poison the client-facing connect address
