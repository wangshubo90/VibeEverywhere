# Architecture Insights — v0.2.2

Observations from deep codebase analysis (April 2026). Each insight describes a
current design property, why it matters at scale, and a suggested direction.

---

## 1. Timer-Polling PTY is a Hidden Scalability Cliff

**Current:** `SessionPump` fires on a fixed timer (~10ms) and iterates all sessions
calling `PollOutput()`. PTY master fds are pollable via epoll/select — they emit
readability events exactly when data arrives.

**Problem:**
- O(N) work every tick regardless of which sessions have output
- Up to 10ms of added output latency even when a single session is active
- Timer fires continuously even when all sessions are idle

**Direction:** Register each PTY master fd with the ASIO `io_context` as an
`async_read` source. Output delivery becomes purely event-driven and the timer can be
demoted to a low-frequency maintenance tick (GC, keepalive, etc). No API change
needed — only `SessionRuntime` and the pump internals change.

---

## 2. Two Local Connection Paths With Different Protocols

**Current:** Local CLI can reach the daemon via:
- HTTP REST + WebSocket on `127.0.0.1:18085`
- Custom binary-framed Unix domain socket at `~/.sentrits/controller.sock`
  (1-byte type + 4-byte big-endian length + payload)

**Problem:** Two code paths for the same job. The UDS protocol is ad-hoc and not
reused anywhere else. Any protocol change must be made in two places.

**Direction:** Evaluate whether the UDS path provides enough latency/throughput
advantage over loopback TCP to justify its maintenance cost. If not, unify on HTTP
+ WebSocket and drop the custom framing. If kept, define the binary protocol
formally so it can be versioned.

---

## 3. Session "Recovery" Is Metadata-Only, Not Communicated Clearly

**Current:** On daemon restart, `LoadPersistedSessions()` restores session records
with `is_recovered = true`. The child process is gone. Clients receive a session
object with output history but no live process.

**Problem:** The UX contract is implicit. A user or client that re-attaches to a
"recovered" session may not know it is dead, especially if the session status is
not surfaced prominently.

**Direction:** Ensure the session status `Exited`/`Error` is immediately and
unambiguously reflected in the API response and WebSocket events for recovered
sessions. Consider a dedicated `Recovered` status variant or a boolean flag in the
session snapshot.

---

## 4. Output Buffer Has No Backpressure Signal

**Current:** `SessionOutputBuffer` is a circular buffer. When full, old data is
silently overwritten. Slow clients that fall behind receive the latest data but
miss the gap.

**Problem:** There is no way for a client to know it missed output. A client trying
to capture a complete session log (e.g., CI integration) will silently produce
incomplete output.

**Direction:** Track a `first_available_sequence` in the buffer alongside
`next_sequence`. When a client requests from a sequence that has been overwritten,
return an error or a gap marker rather than silently serving from the oldest
available. The client can then decide to re-snapshot or report an error.

---

## 5. Admin Port Is an Unauthenticated Trust Boundary

**Current:** `127.0.0.1:18085` accepts all connections without authentication. Any
local process — including processes spawned by sessions themselves — can create,
terminate, or inject input into any session.

**Problem:** On multi-user systems or when sessions run untrusted code, this is a
meaningful privilege escalation surface. A session running `curl localhost:18085`
can control other sessions.

**Direction:** At minimum, document the single-user assumption explicitly. For
multi-user or server deployments, add Unix UID-based access control (check
`SO_PEERCRED` on connections, or use a per-user socket path keyed on UID).

---

## 6. libvterm Enables Size-Independent Replay — Underutilized

**Current:** `TerminalMultiplexer` re-renders the stored terminal state at the
connecting client's terminal size. The raw output is stored pre-libvterm in
`SessionOutputBuffer`.

**Opportunity:** The infrastructure already supports "render this session at a
different size without affecting the child process." A "virtual viewport" feature —
where a viewer can observe a session at their own terminal size without the child
seeing a resize — is architecturally free. The same mechanism could power
screenshot/thumbnail generation for the overview UI.

---

## 7. `local-pty` Is an Orphaned Subsystem

**Current:** `sentrits local-pty [cmd]` is a self-contained PTY runner using a raw
`select()` loop. It duplicates PTY logic, has no persistence, and is not connected
to the session lifecycle.

**Direction:** Either promote it (extract into a documented standalone tool, e.g.,
a lightweight `script`-replacement), or integrate it (make it spawn a daemon-managed
session and attach immediately, so it becomes a thin alias for `start + attach`).
As-is it is a maintenance burden with unclear ownership.

---

## 8. Per-Session File Watchers Don't Scale With Workspace Reuse *(new)*

**Current:** Each `SessionEntry` owns a `GitInspector` and a `WorkspaceFileWatcher`.
If N sessions share the same workspace directory, there are N independent inotify
instances watching the same tree and N independent `git status` processes running on
each change event.

**Problem:**
- FD count grows as `sessions_per_workspace × inotify_fds_per_watcher`
- Redundant git subprocess invocations (each fires independently on the same event)
- Inconsistent git state snapshots — two sessions in the same workspace can briefly
  report different states if their poll intervals are slightly offset
- `inotify` watch descriptors are a limited kernel resource (default 8192 per user)

**Direction: WorkspaceWatcherRegistry**

Introduce a registry keyed on normalized workspace path, owned by `SessionManager`:

```
WorkspaceWatcherRegistry
  key: canonical workspace path (string)
  value: shared WorkspaceWatcher {
    - single inotify/kqueue fd for the tree
    - single GitInspector (runs git status once per change)
    - cached GitState (result of last inspection)
    - event_sequence: uint64_t  (incremented on each change)
    - ref_count: int            (number of sessions using this workspace)
  }
```

Sessions subscribe by workspace path on creation and unsubscribe on exit. Each
session stores only a `last_seen_workspace_sequence` and polls the registry's cached
`GitState` when its own poll tick fires — the same pull model already used by
`SessionOutputBuffer`. The registry tears down the watcher when `ref_count` reaches
zero.

**Benefits:**
- 1 inotify instance + 1 git subprocess per workspace, regardless of session count
- Consistent git state across all sessions in the same workspace
- FD count bounded by unique workspaces, not total sessions
- Fits naturally into the existing timer-poll architecture with no API changes needed

**Implementation touch points:**
- `include/vibe/service/session_manager.h` — add `WorkspaceWatcherRegistry` member
- `src/service/session_manager.cpp` — subscribe on `CreateSession`, unsubscribe on
  session exit
- `include/vibe/session/session_types.h` (or new header) — `WorkspaceWatcher` struct
- `SessionEntry` — replace `unique_ptr<GitInspector>` + `unique_ptr<WorkspaceFileWatcher>`
  with a workspace path + last-seen sequence number
