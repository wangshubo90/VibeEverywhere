# Sentrits Workspace Guardian Roadmap

Status: future roadmap, not an active implementation plan.

This document tightens the Workspace Guardian idea into a staged host-runtime substrate. The goal is to observe workspace reality accurately before building memory, policy, or agent automation on top.

## Goal

Workspace Guardian is a host-level infrastructure layer that observes and normalizes:

- workspace registration
- session-to-workspace attachment
- current working directory changes
- filesystem changes
- git state changes
- process tree activity
- background jobs
- local ports and services started by session processes

This is not agent memory yet. It is the reliable runtime substrate that future memory, evidence, audit, attention, MCP, and agent context APIs can consume.

## Core Invariant

```text
Session does not own workspace state.
Host owns workspace state.
Session attaches to a workspace runtime.

A session may move between directories.
A workspace may have multiple sessions.
A workspace watcher must be shared.
```

Runtime shape:

```text
Sentrits Host
  WorkspaceRegistry
  WorkspaceRuntime[w1]
    FsWatcher
    GitWatcher
    ProcessTracker
    PortDetector
    EventBus
  WorkspaceRuntime[w2]
    FsWatcher
    GitWatcher
    ProcessTracker
    PortDetector
    EventBus
```

## Product Boundary

Build first:

- authoritative workspace/session binding
- normalized workspace events
- cached git state
- process and port observability
- CLI and HTTP read surfaces

Do not build first:

- agent memory
- cross-session orchestration
- permission policy
- sandboxing
- file-read access ledgers
- provider-specific coding-agent semantics

The first version should make human and agent observers agree on "what workspace is this session in, what changed, what processes are alive, and what services are listening."

## Identity Model

Workspace identity should be stable, deterministic, and host-local.

```cpp
struct WorkspaceId {
    std::string canonical_root;
    std::optional<std::string> git_root;
    std::string user_id;
    std::string hash;
};

struct SessionWorkspaceBinding {
    std::string session_id;
    std::string workspace_id;
    std::filesystem::path current_cwd;
    pid_t shell_pid;
    pid_t shell_pgid;
    pid_t shell_sid;
};
```

Recommended `workspace_id` hash inputs:

- canonical workspace root
- detected git root when present
- host user id
- host identity namespace

Avoid using mutable labels, branch names, or session ids in workspace identity.

## Milestone A: Workspace Registry

Objective: create the authoritative mapping between `session_id`, `current_cwd`, `workspace_root`, `git_root`, and `workspace_id`.

On session start:

1. Resolve initial cwd.
2. Detect git root.
3. Choose workspace root.
4. Create or reuse `WorkspaceRuntime`.
5. Attach session to workspace.
6. Increment workspace runtime refcount.

On session end:

1. Detach session from workspace.
2. Decrement runtime refcount.
3. Stop workspace runtime when no active sessions remain, after a short idle grace period.

Initial CLI deliverables:

- `sentrits ws list`
- `sentrits ws status <workspace_id>`
- `sentrits session workspace <session_id>`

HTTP deliverables:

- `GET /workspaces`
- `GET /workspaces/{workspace_id}`
- `GET /sessions/{session_id}/workspace`

Correctness tests:

- two sessions in the same git root share one workspace runtime
- two non-git directories produce separate workspace ids
- ending one of two attached sessions does not stop the shared runtime
- ending the last session stops the runtime after the idle grace period

## Milestone B: CWD Tracking

Objective: track when a session changes directory and update workspace binding without relying on raw terminal text heuristics as the primary source of truth.

Signal priority:

1. Shell integration OSC 7 cwd sequence.
2. `/proc/<shell_pid>/cwd` fallback on Linux.
3. Conservative command parsing heuristic.
4. Initial cwd fallback.

Shell integration sequence:

```sh
printf '\033]7;file://%s%s\033\\' "$HOSTNAME" "$PWD"
```

Host behavior:

```text
cwd_changed(old, new)
  resolve git root
  if same workspace:
    update session.current_cwd
  else:
    detach from old WorkspaceRuntime
    attach to new WorkspaceRuntime
```

Important implementation rule:

- Parse OSC 7 from PTY output before rendering to clients, but do not let cwd parsing mutate terminal bytes delivered to observers unless the escape is known to be metadata-only and safe to suppress.

Deliverables:

- session `current_cwd` is accurate after `cd`
- binding updates when user moves into another project
- clients can display current workspace accurately

Correctness tests:

- OSC 7 changes cwd without visible terminal artifacts
- `cd` from one git repo to another switches workspace runtime
- `/proc/<pid>/cwd` fallback works when shell integration is absent
- deleted cwd or permission denied fallback does not crash the runtime

## Milestone C: Filesystem Watch Plane

Objective: run one watcher per workspace, not one watcher per session.

Linux MVP backend:

- `inotify`
- recursive watches under workspace root
- dynamic watch registration for newly created directories

Tracked events:

- `IN_CREATE`
- `IN_DELETE`
- `IN_MODIFY`
- `IN_CLOSE_WRITE`
- `IN_MOVED_FROM`
- `IN_MOVED_TO`
- `IN_ATTRIB`

Normalized model:

```cpp
enum class FsEventKind {
    Created,
    Modified,
    Deleted,
    Renamed,
    ClosedAfterWrite,
    MetadataChanged
};

struct FsEvent {
    std::string workspace_id;
    std::filesystem::path path;
    FsEventKind kind;
    uint64_t timestamp_ns;
    std::optional<std::filesystem::path> old_path;
};
```

Default ignore rules:

- `.git/`
- `node_modules/`
- `build/`
- `cmake-build-*/`
- `dist/`
- `.cache/`
- `.venv/`
- `__pycache__/`
- `target/`
- `.DS_Store`

Event pipeline:

```text
raw inotify event
  path normalize
  ignore filter
  debounce
  deduplicate
  WorkspaceEventBus
```

Backpressure rules:

- collapse repeated writes to the same path within a debounce window
- cap per-workspace event queue
- emit an overflow event when events are dropped
- never block PTY/session control on filesystem processing

Deliverables:

- `sentrits ws events <workspace_id>`
- `sentrits ws changed <workspace_id>`
- normalized filesystem events in `WorkspaceEventBus`

Correctness tests:

- create/modify/delete/rename are normalized correctly
- ignored directories do not emit user-visible events
- high-frequency writes are debounced
- event overflow is reported explicitly

## Milestone D: Git State Plane

Objective: maintain cached git state per workspace so every session and client does not run `git status` independently.

Refresh triggers:

- `.git/HEAD`
- `.git/index`
- `.git/refs/`
- `.git/packed-refs`
- workspace source-file changes

State model:

```cpp
struct GitState {
    std::string branch;
    std::string head_commit;
    bool dirty;
    std::vector<std::string> modified_files;
    std::vector<std::string> untracked_files;
    uint64_t last_refresh_ns;
};
```

Refresh strategy:

```text
fs event
  mark git_state_dirty
  debounce 300-1000 ms
  run git status --porcelain=v1 -z
  update cached GitState
  emit GitStateChanged when summary changed
```

Implementation constraints:

- run git commands outside critical locks
- enforce timeout and output-size limits
- cache failure state separately from clean/dirty state
- do not refresh on every raw filesystem event

Deliverables:

- `sentrits ws git <workspace_id>`
- workspace summary exposes branch and dirty status
- session cards can read cached workspace git state

Correctness tests:

- dirty/clean transitions are detected
- branch changes are detected
- untracked and modified files are parsed from porcelain `-z`
- git timeout does not block other workspace events

## Milestone E: Process Tracker

Objective: track what the session actually spawned. PTY output shows terminal I/O; process tracking shows runtime activity.

Linux MVP backend:

- `/proc`
- periodic snapshot scanner
- session shell pid, pgid, and sid recorded at session start

Read sources:

- `/proc/<pid>/stat`
- `/proc/<pid>/status`
- `/proc/<pid>/cmdline`
- `/proc/<pid>/cwd`
- `/proc/<pid>/exe`

Attribution strategy:

A process belongs to a session if:

1. Its parent chain reaches `shell_pid`.
2. Its process group or session id matches `shell_pgid` or `shell_sid`.
3. It was previously observed as a descendant and has not exited.

Process model:

```cpp
struct ProcessInfo {
    pid_t pid;
    pid_t ppid;
    pid_t pgid;
    pid_t sid;
    std::string cmdline;
    std::filesystem::path cwd;
    std::filesystem::path exe;
    double cpu_percent;
    uint64_t memory_bytes;
    bool is_background;
};
```

Events:

- `ProcessStarted`
- `ProcessExited`
- `ProcessChangedCwd`
- `BackgroundJobDetected`
- `LongRunningProcessDetected`

Deliverables:

- `sentrits session ps <session_id>`
- `sentrits ws ps <workspace_id>`
- host can detect background jobs
- host can tell whether a command is still running even if PTY is quiet

Correctness tests:

- foreground child process is attributed to the session
- background child process remains visible after prompt returns
- exited processes are removed and emit exit events
- scanner handles permission-denied and short-lived process races

## Milestone F: Port and Service Detector

Objective: detect when session processes start local servers.

Useful cases:

- dev server started
- test server running
- debug server available
- unexpected listener

Linux MVP sources:

- `/proc/net/tcp`
- `/proc/net/tcp6`
- `/proc/<pid>/fd/*`

Approach:

1. Build socket inode to listener map from `/proc/net/tcp*`.
2. Walk tracked process fds.
3. Match socket inode to process.
4. Emit open/closed events.

Event model:

```cpp
struct PortEvent {
    std::string workspace_id;
    std::string session_id;
    pid_t pid;
    uint16_t port;
    std::string protocol;
    std::string bind_address;
    enum { Opened, Closed } kind;
};
```

Deliverables:

- `sentrits session ports <session_id>`
- `sentrits ws ports <workspace_id>`
- session card can show active local servers

Correctness tests:

- simple local HTTP server is detected
- listener close is detected
- IPv4 and IPv6 listeners are normalized
- unowned listeners are not falsely attributed to a session

## Milestone G: Workspace Event Bus

Objective: unify low-level workspace signals into one normalized stream.

Sources:

- PTY metadata events
- CWD events
- filesystem events
- git events
- process events
- port events

Unified event shape:

```cpp
struct WorkspaceEvent {
    std::string event_id;
    std::string workspace_id;
    std::optional<std::string> session_id;
    std::string source;   // fs, git, process, pty, cwd, port
    std::string kind;
    uint64_t timestamp_ns;
    nlohmann::json payload;
};
```

Ring buffer:

- keep last 10,000 events per workspace
- or keep last 24 hours
- whichever limit is reached first

Persistence is explicitly deferred.

Deliverables:

- `sentrits ws timeline <workspace_id>`
- `GET /workspaces/{workspace_id}/events?since=...`
- client subscription API for workspace events

Behavioral rules:

- event stream is eventually consistent, not transactional
- event ids are monotonic within a workspace runtime
- consumers must tolerate missed events and use summary endpoints for reconciliation
- overflow emits an explicit `WorkspaceEventsOverflowed` event

Correctness tests:

- event ordering is stable within a workspace
- `since` pagination does not skip retained events
- overflow is visible to clients
- summary reconciliation works after missed events

## Cross-Platform Abstraction

Define interfaces early, but ship Linux first.

```cpp
class IFsWatcher {
public:
    virtual void start(const WorkspaceId&) = 0;
    virtual void stop() = 0;
};

class IProcessTracker {
public:
    virtual std::vector<ProcessInfo> snapshot() = 0;
};

class IPortDetector {
public:
    virtual std::vector<PortInfo> snapshot() = 0;
};
```

Backend plan:

- Linux filesystem: `inotify`, later `fanotify`
- Linux process: `/proc`
- Linux ports: `/proc/net` plus fd inode mapping
- macOS filesystem: FSEvents
- macOS process: `libproc` / `sysctl`
- macOS ports: lsof-like backend or system APIs
- Windows filesystem: `ReadDirectoryChangesW`
- Windows process: ToolHelp / WMI / ETW later
- Windows ports: IP Helper API

Do not abstract so early that the Linux MVP becomes vague. The interface should be proven by one complete backend first.

## Optional Advanced Linux Track

Later, add OS-level file access tracking:

- which process opened which file
- which process read which file
- which session or workspace that process belonged to

Possible sources:

- `fanotify`
- `auditd`
- eBPF
- `ptrace` / `seccomp` only if sandboxing becomes a real requirement

This is not required for Workspace Guardian MVP. It belongs after workspace binding, process attribution, and filesystem event normalization are stable.

## Recommended Implementation Order

### Milestone A: Workspace Core

- `WorkspaceRegistry`
- `WorkspaceRuntime`
- session attach/detach
- cwd and git root resolution
- CLI and HTTP status commands

### Milestone B: CWD Binding

- OSC 7 parser
- Linux `/proc/<pid>/cwd` fallback
- workspace switch on cwd change
- client-visible current workspace

### Milestone C: Filesystem and Git

- recursive inotify watcher
- ignore rules
- debounce/dedupe
- git state cache
- workspace event stream

### Milestone D: Process Awareness

- `/proc` process scanner
- process tree attribution
- background job detection
- session and workspace process commands

### Milestone E: Runtime Observability

- port detector
- PTY metadata event merge
- workspace timeline
- client subscription API

### Milestone F: Advanced OS Signals

- file access tracking
- read ledger
- process-level file access attribution

## MVP Cut

The smallest useful MVP should include:

- workspace registry and shared runtime
- session attach/detach
- initial cwd and git root detection
- OSC 7 cwd tracking
- Linux inotify watcher with ignore/debounce
- cached git status
- `sentrits ws list`
- `sentrits ws status <workspace_id>`
- `sentrits ws events <workspace_id>`
- `GET /workspaces`
- `GET /workspaces/{workspace_id}`
- `GET /workspaces/{workspace_id}/events?since=...`

Defer process tracking and port detection if needed, but design the event bus so they can be added without reshaping client APIs.

## Risks And Design Checks

### Risk: workspace explosion

A session can `cd` through many directories. Do not create durable workspace identities for every transient cwd. Prefer git root when available and apply idle cleanup for detached runtimes.

### Risk: watcher overload

Large repos can generate huge event streams. Debounce, ignore, queue caps, and overflow events are part of the MVP, not polish.

### Risk: git refresh storms

Filesystem changes can trigger many git refreshes. Git state must be cached, debounced, timeout-bounded, and run outside locks.

### Risk: process attribution is approximate

Process trees are race-prone. Treat process attribution as best-effort observability, not security authentication.

### Risk: shell integration is not always present

OSC 7 should be best signal, not only signal. Linux `/proc/<shell_pid>/cwd` fallback is required for useful MVP behavior.

### Risk: client API coupling

Clients should consume summaries and event streams, not raw watcher internals. Keep event kinds and payload schemas versioned and additive.

## What This Enables Later

Once this substrate exists, upper layers become natural:

- workspace memory
- topic thread memory
- agent resume capsules
- audit trail
- attention system improvements
- agent context API
- MCP server
- workspace-level security policy

Those should be built after the observer is reliable.

First build the observer. Then build intelligence on top.
