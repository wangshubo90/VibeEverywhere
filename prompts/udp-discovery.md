# Runtime Prompt: UDP Discovery

Work in: `~/dev/VibeEverywhere-runtime-udp-discovery`

## Goal

Add LAN discovery support to the runtime in the existing C++20 / CMake codebase.

The runtime should:

- advertise a small, non-sensitive discovery payload over UDP broadcast
- expose `GET /discovery/info` so a client can verify a discovered host over HTTP or HTTPS

This is part of the runtime-side discovery contract already described in:

- `development _memo/api_and_event_schema.md`
- `development _memo/client_runtime_parallel_plan.md`

## Codebase Context

Fit the work into the current seams instead of inventing a parallel architecture.

Existing areas to extend:

- `src/net/http_shared.cpp` and `src/net/json.cpp` for HTTP route handling and JSON serialization
- `src/net/http_server.cpp` and `include/vibe/net/http_server.h` for listener/runtime wiring
- `src/main.cpp` only if runtime startup configuration needs to surface new discovery behavior
- `include/vibe/store/host_config_store.h` and the host identity model as the source of host metadata
- `tests/net/http_shared_test.cpp`, `tests/net/http_json_test.cpp`, and `tests/net/http_server_integration_test.cpp` for coverage

Keep naming and JSON style consistent with the existing `/host/info` implementation.

## Product Constraints

- discovery is advisory only
- pairing and authorization still gate real access
- discovery must not expose privileged data, secrets, tokens, filesystem paths, or admin-only details
- the host admin UI remains localhost-only
- the remote listener remains the client-facing verification surface
- prefer additive changes over refactors unrelated to discovery

## Required Public Contract

### UDP Payload

Freeze these payload fields:

- `hostId`
- `displayName`
- `remoteHost`
- `remotePort`
- `protocolVersion`
- `tls`

Do not add extra externally visible fields unless there is a strong runtime-side reason and the tests/docs are updated to justify it.

### HTTP Verification Endpoint

Add:

- `GET /discovery/info`

Behavior:

- return the same core metadata as the UDP payload
- expose it on the remote listener path clients already use
- keep the response format stable and machine-friendly JSON
- if TLS is enabled for the remote listener, reflect that truthfully in the payload

## Design Guidance

Implement conservatively.

- Introduce a dedicated discovery payload/read model instead of hand-building JSON at the call site
- Reuse existing host identity and remote TLS configuration rather than duplicating metadata sources
- Treat wildcard bind addresses carefully: if the runtime is configured with `0.0.0.0`, do not silently claim that is a useful client-facing discovery address unless the chosen design explicitly justifies it
- Keep the UDP broadcaster owned by the runtime/server lifecycle so it starts and stops with the daemon
- Use a conservative broadcast cadence and document it
- Prefer simple lifecycle management that can be tested without brittle timing-based tests

## Suggested Execution Order

1. add or freeze the discovery payload model and JSON serialization
2. add `GET /discovery/info` with tests first
3. add the UDP broadcaster and runtime lifecycle wiring
4. document cadence, assumptions, and any configuration choices

## Testing Requirement

Add tests wherever practical.

At minimum:

- request/response coverage for `GET /discovery/info`
- serialization coverage for the discovery payload helper/model
- coverage proving TLS state is reflected correctly if the endpoint shares logic with `/host/info`
- coverage for any endpoint behavior that differs between configured and default host identity
- avoid timing-fragile tests for raw UDP networking if the behavior can be validated through smaller seams

Run and report:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

## Non-Goals

- client-side discovery UI
- pairing flow changes
- mDNS or cross-subnet discovery
- NAT traversal
- notification work
- broad HTTP server refactors unrelated to discovery

## Commit Guidance

Commit with proper granularity.

Recommended split:

1. `add discovery info api contract and tests`
2. `add udp discovery broadcaster`
3. `document udp discovery behavior`

## Final Response Format

Report:

- what you changed
- assumptions or unresolved design choices
- files changed
- tests/build results
- commit hashes
