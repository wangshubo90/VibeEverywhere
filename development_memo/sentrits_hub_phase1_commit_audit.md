# Sentrits-Hub Phase 1 Commit Audit

## Findings

1. High: HTTPS in the Core Hub client does not verify that the certificate
   matches the requested hostname.

   Relevant code:
   - [`src/net/hub_client.cpp`](/home/shubow/dev/Sentrits-Core/src/net/hub_client.cpp)

   The client sets `verify_peer` and SNI, but it does not install hostname
   verification such as `ssl::host_name_verification`. That means a certificate
   signed by a trusted CA for the wrong hostname may still be accepted.

   Why this matters:
   - the Hub client sends bearer-token heartbeats
   - TLS without hostname verification is not sufficient protection

   Recommended fix:
   - add explicit hostname verification for HTTPS connections
   - keep failing closed on hostname mismatch

2. High: the Core lifecycle fix closes one shutdown race, but the broader
   concurrency problem remains.

   Relevant code:
   - [`src/net/hub_client.cpp`](/home/shubow/dev/Sentrits-Core/src/net/hub_client.cpp)
   - [`include/vibe/service/session_manager.h`](/home/shubow/dev/Sentrits-Core/include/vibe/service/session_manager.h)
   - [`src/net/http_server.cpp`](/home/shubow/dev/Sentrits-Core/src/net/http_server.cpp)

   `HubClient` calls `session_manager_.ListSessions()` from its own background
   thread. `SessionManager` is also used and mutated by the server/listener
   side and is not structured here as a thread-safe object. Stopping the Hub
   thread before `Shutdown()` avoids one specific race, but normal runtime
   access can still interleave with `ListSessions()`.

   Recommended fix:
   - move heartbeat snapshot collection onto the same owning server thread /
     `io_context`, or
   - add an explicit synchronization boundary for session-summary reads.

3. Medium: `Sentrits-Hub` still does not commit `go.sum`, so the repository is
   not build-reproducible as committed.

   Relevant repo state:
   - [`go.mod`](/home/shubow/dev/Sentrits-Hub/go.mod)
   - missing `go.sum`

   The repo currently requires a post-clone `go mod tidy` step before builds
   are deterministic. For a committed Phase 1 service slice, that should be
   part of the checked-in state.

   Recommended fix:
   - run `go mod tidy`
   - commit `go.sum`

## What Looks Good

- Cross-account host takeover is blocked correctly in `Sentrits-Hub`.
- Offline hosts no longer expose stale session inventory.
- Core startup no longer starts the Hub thread before listener bind succeeds.
- Core stop now stops the Hub thread before triggering server shutdown.

## Summary

The Hub control-plane slice is in reasonable shape. The remaining material
issues are:

- HTTPS hostname verification in the Core Hub client
- unsynchronized cross-thread access to `SessionManager`
- missing `go.sum` in `Sentrits-Hub`

The first two should be fixed before building more behavior on the outbound
heartbeat path.
