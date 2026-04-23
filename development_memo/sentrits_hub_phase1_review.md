# Sentrits-Hub Phase 1 Review

## Findings

1. High: `HubClient` reads `SessionManager` from its own background thread with
   no synchronization, while `SessionManager` is also mutated and shut down on
   the server thread.

   Relevant code:
   - [`src/net/hub_client.cpp`](/home/shubow/dev/Sentrits-Core/src/net/hub_client.cpp)
   - [`src/net/http_server.cpp`](/home/shubow/dev/Sentrits-Core/src/net/http_server.cpp)

   `RunLoop()` calls `SendHeartbeat()`, which calls `session_manager_.ListSessions()`.
   In parallel, shutdown runs through the server stop path and calls
   `session_manager_.Shutdown()`. `SessionManager` is not structured here as a
   thread-safe object. Phase 1 therefore introduces a cross-thread data race at
   the host runtime boundary.

   Recommended fix:
   - collect heartbeat session snapshots on the same thread / `io_context` that
     owns session state, or
   - add an explicit synchronized snapshot interface instead of reading session
     state directly from the Hub background thread.

2. Medium: the Hub server still carries dead configuration surface in heartbeat
   handling.

   Relevant code:
   - [`internal/api/hosts.go`](/home/shubow/dev/Sentrits-Hub/internal/api/hosts.go)
   - [`internal/db/queries.go`](/home/shubow/dev/Sentrits-Hub/internal/db/queries.go)

   `UpdateHostHeartbeat(...)` takes `heartbeatTimeoutS`, and the handler passes
   it, but the value is unused. This is not functionally harmful, but it is
   misleading because it suggests heartbeat update semantics depend on the
   timeout when they do not.

   Recommended fix:
   - remove the unused parameter, or
   - use it in a way that materially affects update behavior.

3. Low: `HubClient` accepts and stores `host_id` but does not use it.

   Relevant code:
   - [`include/vibe/net/hub_client.h`](/home/shubow/dev/Sentrits-Core/include/vibe/net/hub_client.h)
   - [`src/net/hub_client.cpp`](/home/shubow/dev/Sentrits-Core/src/net/hub_client.cpp)

   Heartbeat auth is token-based and the current endpoint does not require
   `host_id` in the payload. This is just unused scaffolding, but it makes the
   interface noisier than necessary.

   Recommended fix:
   - remove `host_id` from the client interface until needed, or
   - start using it explicitly in a validated payload contract.

## What Looks Good

- Cross-account host takeover is now blocked correctly in `Sentrits-Hub`.
- Offline hosts no longer expose stale session inventory.
- The Core Hub client now performs real TLS for `https://` Hub URLs.
- Core startup no longer starts the Hub heartbeat thread before listener bind
  succeeds.

## Summary

Phase 1 is in much better shape than the earlier slice. The main remaining
issue is not API shape or token handling; it is the thread-safety boundary
between `HubClient` and `SessionManager`. That should be fixed before building
further behavior on top of the outbound heartbeat path.
