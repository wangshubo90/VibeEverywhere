# Runtime Prompt: UDP Discovery

Work in: `~/dev/VibeEverywhere-runtime-udp-discovery`

## Goal

Add LAN discovery support to the runtime.

The host should advertise a small discovery payload over UDP broadcast, and the runtime should also expose a discovery-info HTTP endpoint so clients can verify a discovered host over HTTP.

## Product Constraints

- discovery is advisory only
- pairing and authorization still gate real access
- discovery must not expose privileged data
- the host admin UI remains localhost-only
- the remote listener remains the client-facing surface

## Required Backend Contract

### UDP Payload

Freeze payload fields as:

- `hostId`
- `displayName`
- `remoteHost`
- `remotePort`
- `protocolVersion`
- `tls`

### HTTP Verification Endpoint

Add:

- `GET /discovery/info`

Return the same core metadata advertised over UDP.

## Suggested Scope

1. add a discovery payload model
2. add a UDP broadcaster owned by the runtime
3. define broadcast interval / TTL behavior conservatively
4. expose `GET /discovery/info`
5. add tests for payload serialization and the new endpoint
6. update runtime docs as needed

## Testing Requirement

Add tests wherever practical.

At minimum:

- request/response coverage for `GET /discovery/info`
- unit coverage for payload serialization/formatting if you introduce helpers
- avoid untested networking logic if the design can be unit-tested indirectly

Run and report:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

## Commit Guidance

Commit with proper granularity.

Recommended split:

1. `add discovery info api contract and tests`
2. `add udp discovery broadcaster`
3. `document udp discovery behavior`

## Non-Goals

- client-side discovery UI
- pairing flow changes
- mDNS or cross-subnet discovery
- notification work

## Final Response Format

Report:

- what you changed
- assumptions
- files changed
- tests/build results
- commit hashes
