# Sentrits-Hub Phase 1 Test Plan

## Goal

Add focused unit and integration coverage for:

- Hub account / device / host auth
- host registration ownership rules
- heartbeat metadata replacement semantics
- online/offline visibility behavior
- Core outbound Hub heartbeat client
- Core server / Hub client lifecycle behavior

The plan should favor small deterministic tests first, then a thin
integration layer around actual HTTP behavior.

## Sentrits-Hub Unit Tests

### 1. Host registration ownership and token rotation

Files:
- `/home/shubow/dev/Sentrits-Hub/internal/db/queries.go`
- `/home/shubow/dev/Sentrits-Hub/internal/api/hosts.go`

Tests:
- register new host under account A succeeds
- re-register same `host_id` under account A rotates token and succeeds
- register same `host_id` under account B fails with conflict
- heartbeat authenticated with old rotated token fails
- heartbeat authenticated with new token succeeds

Why:
- this is the critical ownership boundary in Phase 1

### 2. Device/account auth paths

Files:
- `/home/shubow/dev/Sentrits-Hub/internal/api/middleware.go`
- `/home/shubow/dev/Sentrits-Hub/internal/api/auth.go`
- `/home/shubow/dev/Sentrits-Hub/internal/api/devices.go`

Tests:
- account JWT authenticates account routes
- invalid JWT is rejected
- device token authenticates `/devices/hosts`
- invalid device token is rejected
- device token resolves correct `account_id` and `device_id`

Why:
- this is the basic Phase 1 auth surface

### 3. Heartbeat session replacement semantics

Files:
- `/home/shubow/dev/Sentrits-Hub/internal/db/queries.go`

Tests:
- first heartbeat inserts sessions
- second heartbeat with different set replaces, not merges
- empty heartbeat clears prior session set
- updated titles / attention values fully replace prior values

Why:
- mobile inventory correctness depends on this being exact

### 4. Online/offline visibility behavior

Files:
- `/home/shubow/dev/Sentrits-Hub/internal/db/queries.go`
- `/home/shubow/dev/Sentrits-Hub/internal/api/hosts.go`

Tests:
- host is online when `last_seen_at` is within timeout
- host is offline when outside timeout
- offline host returns empty session list even if stale `host_sessions` rows exist
- online host returns session inventory

Why:
- this is the user-facing presence rule for MVP-A

## Sentrits-Hub Integration Tests

### 5. End-to-end HTTP API smoke flow

Spin up:
- temporary Postgres test database
- real Hub HTTP server

Flow:
- register account
- obtain JWT
- register device
- register host
- send heartbeat
- list hosts with account JWT
- list hosts with device token

Assertions:
- host appears online
- session inventory matches heartbeat payload
- only coarse metadata is returned

Why:
- this validates that the API wiring and middleware stack behave together

### 6. Host ownership conflict integration

Flow:
- account A registers host `h_1`
- account B attempts to register host `h_1`

Assertions:
- second request returns conflict
- old token remains authoritative
- account B cannot take over heartbeat path

Why:
- this is the highest-risk trust regression for Hub Phase 1

## Sentrits-Core Unit Tests

### 7. `HostIdentity` Hub field persistence

Files:
- `/home/shubow/dev/Sentrits-Core/include/vibe/store/host_config_store.h`
- `/home/shubow/dev/Sentrits-Core/src/store/file_stores.cpp`
- `/home/shubow/dev/Sentrits-Core/tests/store/file_stores_test.cpp`

Tests:
- `hub_url` and `hub_token` round-trip
- missing fields load as `nullopt`
- empty strings do not force malformed state

Why:
- this is the persisted Core/Hub config contract

### 8. Hub URL parsing and request construction

Files:
- `/home/shubow/dev/Sentrits-Core/src/net/hub_client.cpp`

Tests:
- parse `http://host`
- parse `https://host`
- parse custom ports
- reject malformed URLs
- heartbeat JSON contains only:
  - `session_id`
  - `title`
  - `lifecycle_state`
  - `attention_state`
  - `attention_reason`

Why:
- keeps the payload intentionally coarse and catches drift early

### 9. HTTPS verification behavior

Files:
- `/home/shubow/dev/Sentrits-Core/src/net/hub_client.cpp`

Tests:
- HTTPS succeeds against trusted certificate with matching hostname
- HTTPS fails on hostname mismatch
- HTTPS fails on untrusted certificate chain

Why:
- bearer-token traffic depends on this being correct

### 10. Hub client lifecycle tests

Files:
- `/home/shubow/dev/Sentrits-Core/include/vibe/net/http_server.h`
- `/home/shubow/dev/Sentrits-Core/src/net/http_server.cpp`
- `/home/shubow/dev/Sentrits-Core/src/net/hub_client.cpp`

Tests:
- Hub client does not start when `hub_url` or `hub_token` missing
- Hub client starts only after server bind succeeds
- Hub client stops before shutdown path tears down sessions
- repeated `Stop()` is safe / idempotent

Why:
- this is where the current lifecycle fragility lives

## Sentrits-Core Integration Tests

### 11. Outbound heartbeat to test Hub

Spin up:
- lightweight local HTTP test server that captures requests
- `HttpServer` configured with test `hub_url` and `hub_token`

Assertions:
- heartbeat is emitted after startup
- auth header uses bearer token
- payload contains session inventory from current `SessionManager`
- non-2xx response is logged/non-fatal

Why:
- validates the real Core -> Hub wire path

### 12. Concurrency / ownership boundary test

Goal:
- prove the chosen synchronization model for heartbeat collection is safe

Possible shapes:
- if heartbeat collection is moved onto server thread:
  - assert it runs via the owning executor / event loop
- if explicit locking is added:
  - stress create/list/stop during heartbeat reads

Why:
- this is the main correctness risk in the current Core Phase 1 path

## Recommended Order

1. Core unit tests for persistence + URL/payload parsing
2. Hub unit tests for ownership, auth, and session replacement
3. Core HTTPS verification tests
4. Hub HTTP integration flow
5. Core outbound heartbeat integration test
6. Concurrency/lifecycle test after the synchronization fix is chosen

## Minimum Gate Before Phase 2

Before building further Hub behavior, require:

- Hub ownership conflict tests passing
- Hub heartbeat replacement tests passing
- Core HTTPS verification tests passing
- Core outbound heartbeat integration test passing
- one explicit test covering the final `SessionManager` / Hub heartbeat
  synchronization model
