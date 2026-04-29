# Evidence MVPv1 Implementation Plan

## Goal

Deliver one narrow feature well:

Agent and human read the same host-owned evidence from managed log sessions, and Sentrits records what was read.

MVPv1 does not include collaboration requests, approvals, input injection, subscriptions, or general info sessions. It focuses only on:
- pipe-based log sessions to evidence
- observable observation
- shared human/agent view of the same filtered data

## Product Loop

1. User starts a managed log session, for example `sentrits session start-log --name runtime -- <cmd>`.
2. Agent calls a Sentrits evidence command, for example:
   - `sentrits evidence tail --session <id> --lines 120`
   - `sentrits evidence search --session <id> --pattern timeout`
   - `sentrits evidence context --session <id> --entry <entry_id> --before 40 --after 80`
3. Host reads/filter/searches the session output.
4. Host returns a bounded result to the agent.
5. Host records an `ObservationEvent`.
6. Host actively posts or exposes that event to clients.
7. Client shows a notification/card such as `Session X searched log session Y`.
8. Human opens a lightweight evidence viewer showing the same lines, filters, highlights, and range that the agent saw.

The feature is successful only if the human can reproduce the agent's evidence view from the client.

## Design Lessons From Chrome DevTools MCP

Use three patterns from the DevTools MCP logging design.

### 1. Collector owns source-scoped evidence

DevTools MCP uses per-page collectors. Sentrits should use per-source collectors.

For MVPv1:
- source is `managed_log_session`
- source id is the log session id
- evidence storage is a bounded host-side log buffer
- stable evidence entries are assigned at ingestion

Do not use the interactive PTY buffer as the first evidence source. A pipe-based log session gives natural line framing, timestamps, and bounded host-side retention.

### 2. Response assembly is late-bound

DevTools MCP tools request evidence inclusion, then a response assembler formats the final response.

Sentrits should use the same split:
- read/filter/search code returns structured evidence records
- an `EvidenceResponseAssembler` produces:
  - agent-facing text
  - structured JSON
  - observation event payload
  - client replay descriptor

This avoids divergent agent and client views.

### 3. Observation events are emitted at response finalization

The right place to emit `ObservationEvent` is after the evidence response is assembled, because only then do we know:
- source
- query/filter
- selected range
- matched entries
- highlights
- exact text returned
- result count

Do not emit observation events from lower-level buffer reads.

## MVPv1 Scope

### In Scope

- managed log session output as the only evidence source
- stdout/stderr pipe ingestion
- bounded host-side log ring buffer
- bounded tail by lines
- bounded range by revision or evidence entry id
- context around an entry id
- substring search
- optional regex search if cheap to add with standard library support
- host-side highlight ranges for matched terms
- eviction accounting with `oldest_revision`, `latest_revision`, `dropped_entries`, and `dropped_bytes`
- in-memory observation event ring buffer
- HTTP endpoint for clients to list recent observations
- agent-facing CLI commands
- structured JSON output for agents
- lightweight client replay descriptor

### Out Of Scope

- strong authentication
- process-tree ancestry enforcement
- approval flow
- request/job routing
- stdin injection
- interrupt/terminate/control handoff
- persistent observation storage
- subscriptions/watchers
- PTY evidence projection
- terminal ANSI/cursor interpretation
- continuous client log streaming
- general info sessions
- visual evidence
- mobile-side heavy search or regex

## Core Data Model

### EvidenceSource

```cpp
enum class EvidenceSourceKind {
  ManagedLogSession,
};

struct EvidenceSourceRef {
  EvidenceSourceKind kind;
  SessionId session_id;
};
```

MVPv1 only supports `ManagedLogSession`.

### LogSession

MVPv1 should introduce a separate managed session category for logs.

```cpp
enum class SessionCategory {
  InteractivePty,
  LogPipe,
};
```

`LogPipe` sessions:
- run a child process with stdout/stderr captured through pipes
- do not allocate a PTY
- are not rendered as terminal focus/control views
- are evidence sources first
- may still appear in session lists as log sources

This avoids the hard PTY projection problems in MVPv1: ANSI stripping, cursor movement, bare carriage-return rewrites, and terminal scrollback reconstruction.

### EvidenceEntry

Line-oriented view over log session output.

```cpp
struct EvidenceEntry {
  std::string entry_id;
  EvidenceSourceRef source;
  uint64_t revision = 0;
  uint64_t byte_start = 0;
  uint64_t byte_end = 0;
  std::chrono::system_clock::time_point timestamp;
  std::string stream; // stdout or stderr
  std::string text;
  bool partial = false;
};
```

`entry_id` should be stable enough for short-term replay.

Recommended MVP format:

```text
log:<session_id>:rev:<revision>
```

This is not a permanent global ID, but it is deterministic and debuggable.

### EvidenceHighlight

```cpp
enum class EvidenceHighlightKind {
  Match,
  Warning,
  Error,
  Preset,
};

struct EvidenceHighlight {
  std::string entry_id;
  size_t start = 0;
  size_t length = 0;
  EvidenceHighlightKind kind;
};
```

For MVPv1, highlights are byte offsets in UTF-8 strings. That is acceptable because the first target is logs and agent-readable text. A later UI layer can improve Unicode column handling.

Wire format values:
- `match`
- `warning`
- `error`
- `preset`

### EvidenceOperation

Use one operation enum everywhere. Do not use a string in `EvidenceResult` and a separate enum in `ObservationEvent`.

```cpp
enum class EvidenceOperation {
  Tail,
  Search,
  Context,
  Range,
};
```

Wire format values:
- `tail`
- `search`
- `context`
- `range`

### EvidenceResult

```cpp
struct EvidenceResult {
  EvidenceSourceRef source;
  EvidenceOperation operation;
  std::string query;
  uint64_t revision_start = 0;
  uint64_t revision_end = 0;
  uint64_t oldest_revision = 0;
  uint64_t latest_revision = 0;
  std::vector<EvidenceEntry> entries;
  std::vector<EvidenceHighlight> highlights;
  bool truncated = false;
  bool buffer_exhausted = false;
  uint64_t dropped_entries = 0;
  uint64_t dropped_bytes = 0;
  std::string error_code;
  std::string replay_token;
};
```

`replay_token` is a pinned descriptor:
- source session id
- operation
- query
- revision range
- limit/context values

Pinned MVPv1 format:

```text
base64url(json({
  "source_type": "managed_log_session",
  "source_id": "...",
  "operation": "search",
  "query": "timeout",
  "revision_start": 123,
  "revision_end": 456,
  "limit": 100,
  "context_before": 0,
  "context_after": 0
}))
```

The token is not a secret. It is a reproducible evidence query descriptor. If the underlying session output has been evicted, replay must return a clear `buffer_exhausted` result instead of silently returning the wrong range.

### ObservationEvent

```cpp
struct ObservationEvent {
  std::string id;
  std::chrono::system_clock::time_point timestamp;
  std::string actor_session_id;
  std::string actor_title;
  std::string actor_id;
  pid_t pid = 0;
  uid_t uid = 0;
  gid_t gid = 0;
  EvidenceOperation operation;
  EvidenceSourceRef source;
  std::string source_title;
  std::string query;
  uint64_t revision_start = 0;
  uint64_t revision_end = 0;
  size_t result_count = 0;
  bool truncated = false;
  std::string summary;
  std::string replay_token;
};
```

Actor identity can be best-effort in MVPv1:
- prefer `SENTRITS_SESSION_ID`
- record peer credentials when available
- allow unauthenticated local calls during prototype if needed
- set `actor_id = actor_session_id` until a separate actor/role model exists

Do not leave `actor_id` undefined. In MVPv1, the actor is the managed session that performed the evidence read.

## Log Buffer and Backpressure

Pipe log sessions need bounded retention from day one.

The host must never stream an unbounded log to iOS. Clients receive observation events and pull bounded evidence slices only when the user opens a viewer.

### LogBuffer responsibilities

Create a dedicated line-oriented `LogBuffer` for `LogPipe` sessions.

Responsibilities:
- ingest stdout/stderr byte chunks
- split into newline-terminated entries
- keep partial line state per stream
- assign monotonic `revision` values
- timestamp each completed line at ingestion time
- maintain byte offsets for diagnostics and future replay
- enforce bounded retention by bytes and entries
- expose `oldest_revision`, `latest_revision`, `dropped_entries`, and `dropped_bytes`

Recommended MVP defaults:

```text
max_bytes = 16MB per log session
max_entries = 50,000 per log session
default_tail_limit = 200 lines
max_tail_limit = 1,000 lines
default_search_limit = 100 matches
max_search_limit = 1,000 matches
```

These values are starting points. They should be configurable later.

### Line framing policy

For pipe sessions:
- `\n` completes a line
- `\r\n` is normalized to one line ending
- stdout and stderr keep separate partial-line buffers
- a partial final line can be returned with `partial = true` for tail reads

Bare `\r` is not a first-class MVP concern for log sessions. If needed, treat it as a progress-line rewrite inside the current partial line, not as a durable log entry.

### ANSI policy

MVPv1 should prefer disabling color for log sessions when Sentrits launches the process:
- set `NO_COLOR=1` where appropriate
- avoid allocating a PTY

If ANSI appears in pipe output anyway, stripping can be added inside `LogBuffer` or `EvidenceService`, but it is no longer the central MVP risk.

### Eviction policy

When the buffer exceeds `max_bytes` or `max_entries`:
- evict oldest completed entries first
- increment `dropped_entries` and `dropped_bytes`
- update `oldest_revision`
- never block iOS by trying to deliver evicted history

If a replay or context request points to evicted data:
- return `buffer_exhausted = true` if the anchor revision is gone
- return fewer before/after lines with `truncated = true` if only surrounding context is gone

### Context reads

Context is simple for `LogBuffer`:
- parse `entry_id` into a revision
- find the revision in the retained entries with `index = revision - oldest_revision`
- return `before` and `after` entries by index

No backward raw-byte scan is needed.

Because revisions are monotonic and retained entries are contiguous after eviction, context lookup should be O(1) after validating:
- `revision >= oldest_revision`
- `revision <= latest_revision`
- computed index is inside the ring's retained entry count

### Client pressure rule

iOS should never receive the whole log session by default.

Allowed client payloads:
- observation events
- bounded tail result
- bounded search result
- bounded context result
- replay result

Disallowed in MVPv1:
- continuous log streaming to iOS
- sending the entire retained buffer
- client-side regex over large logs

## Client Attention Signal

Observation events are not just audit records. In MVPv1 they are the active signal that tells the human, "there is evidence here."

When a managed session reads evidence, the host should create an `ObservationEvent` and make it visible to connected clients quickly.

Minimum event wording:

```text
Session X opened log session Y
Session X searched log session Y for "timeout"
Session X tailed 120 lines from log session Y
Session X opened context around rev 1842 in log session Y
```

The client should be able to route the event into one of two surfaces:
- notification/card: lightweight prompt that evidence was read
- evidence panel: multi-source panel where each source/query can become a tab

The exact UI can be decided later, but the API should already carry enough data for both:
- actor session id/title
- source session id/title
- action
- query
- result count
- replay token
- revision range
- timestamps

The client should not need to reconstruct the message by fetching unrelated state if the event already has enough summary data.

## Service Boundaries

### SessionManager

Add read-only methods that own all access to log session buffers:

```cpp
std::optional<EvidenceResult> TailEvidence(
    const SessionId& source,
    const EvidenceTailOptions& options);

std::optional<EvidenceResult> SearchEvidence(
    const SessionId& source,
    const EvidenceSearchOptions& options);

std::optional<EvidenceResult> ContextEvidence(
    const SessionId& source,
    const EvidenceContextOptions& options);
```

These methods should:
- acquire the appropriate manager lock
- locate the log session buffer safely
- enforce read limits
- read retained entries by revision
- never expose direct runtime pointers or unbounded buffers

### EvidenceService

New service layer responsible for:
- calling `SessionManager` read methods
- applying substring/regex matching
- adding highlights
- creating replay tokens
- finalizing `EvidenceResult`

### ObservationStore

In-memory ring buffer for MVPv1:
- bounded count, for example 500 events
- list by latest
- list by actor session
- list by source session

Do not persist yet, but keep schema stable enough to persist later.

### EvidenceResponseAssembler

Takes `EvidenceResult` and caller context, then produces:
- text output for CLI
- JSON output for CLI/API
- `ObservationEvent`
- client replay descriptor

This is where observation events should be emitted.

## API Surface

### Agent-Facing CLI

Native commands first:

```bash
sentrits evidence list
sentrits evidence tail --session <id> --lines 120
sentrits evidence search --session <id> --pattern "timeout"
sentrits evidence context --session <id> --entry <entry_id> --before 40 --after 80
```

Useful aliases later:

```bash
sentrits evidence grep --session <id> "timeout"
sentrits evidence errors --session <id>
```

Output modes:
- default: compact text for agents
- `--json`: structured result

The CLI should call the same host API path as future agent tools. It must not scrape terminal output directly.

Actor attribution requirement:
- CLI must propagate `SENTRITS_SESSION_ID` as `actor_session_id`
- if using HTTP for MVPv1, send it as a header such as `X-Sentrits-Actor-Session`
- if using Unix socket later, also record peer credentials

Without actor attribution, the observation event loses the main product value: showing which agent/session read what.

### Host HTTP API For Clients

Add client-readable endpoints:

```http
GET /evidence/sources
GET /evidence/sessions/:id/tail?lines=120
GET /evidence/sessions/:id/search?pattern=timeout&limit=100
GET /evidence/sessions/:id/context?entryId=...&before=40&after=80
GET /evidence/replay?token=...
GET /observations?limit=100
```

For MVPv1, the CLI can also use HTTP if that is simpler than a Unix gateway. The gateway decision should not block the evidence model.

If HTTP is used by the CLI in MVPv1:
- require `X-Sentrits-Actor-Session` when the environment has `SENTRITS_SESSION_ID`
- record unknown peer credentials as zero/empty
- keep the field names identical to the later Unix socket path

Observation emission contract:
- requests with `X-Sentrits-Actor-Session` emit `ObservationEvent`
- client viewer requests without that header do not emit `ObservationEvent`
- replay calls from the client are view actions, not new agent observations

This prevents the human opening the evidence viewer from creating spurious `Session X read log Y` events.

### Event Push

If existing session or host websocket events can carry observation events cheaply, add:

```json
{
  "type": "observation.created",
  "event": { ... }
}
```

If not, polling `GET /observations` is acceptable for MVPv1.

Preferred MVP behavior:
- use push if there is an existing host/session event channel that can carry `observation.created`
- fall back to short polling if push would delay the Core evidence work
- in both cases, the observation payload must include a replay token so the client can open the evidence viewer immediately

## Client UX MVP

### Notification/Card

Show recent observation events:
- actor/session
- action
- source session title
- query
- result count
- timestamp

Example:

```text
Codex searched "Runtime Log" for timeout
17 matches
```

Click behavior:
- opens the evidence viewer using `replay_token`
- focuses or creates a tab for that source/query
- does not open a terminal control view

### Evidence Viewer

This is not a terminal view.

It should render:
- source session title
- action and query
- result count
- line list
- highlighted matches
- surrounding context when available
- revision/range metadata in a compact detail area
- context action for each line using its `entry_id`

iOS should not run heavy regex or filtering. The host returns already-filtered entries and highlight ranges.

Evidence panel shape:
- multi-tab capable
- each tab can represent one source/query/replay token
- multiple monitored sources can coexist
- tabs can be created from observation cards or direct user action

## Implementation Order

### Step 1: Core structs and JSON

Add:
- `EvidenceSourceRef`
- `EvidenceEntry`
- `EvidenceHighlight`
- `EvidenceResult`
- `ObservationEvent`

Add JSON serializers and tests.

Pin in this step:
- replay token schema
- `buffer_exhausted` response behavior
- `actor_id = actor_session_id` for MVPv1
- `partial` field on evidence entries
- `LogPipe` session category
- `LogBuffer` retention metadata

### Step 2: LogPipe session and LogBuffer

Add managed log sessions backed by stdout/stderr pipes and a bounded `LogBuffer`.

API shape:

Prefer a dedicated creation path over overloading the existing interactive session path:

```cpp
struct LogSessionCreateRequest {
  std::string title;
  std::vector<std::string> command;
  std::optional<std::filesystem::path> working_directory;
  std::map<std::string, std::string> environment;
};

SessionId CreateLogSession(const LogSessionCreateRequest& request);
```

HTTP/CLI can expose this as:

```bash
sentrits session start-log --name runtime -- <cmd>
```

This keeps `InteractivePty` and `LogPipe` launch behavior explicit. A shared internal `SessionCategory` field is still useful for summaries and routing.

Implement:
- process launch without PTY
- stdout/stderr pipe readers
- line-oriented ingestion
- per-stream partial-line buffers
- monotonic revisions
- bounded retention by bytes and entries
- eviction counters

Tests:
- stdout and stderr lines are captured with revisions
- timestamps are assigned at completed-line ingestion time
- entry ids use `log:<session_id>:rev:<revision>`
- partial final line can be tailed
- retention evicts oldest entries
- dropped counters update
- context revision lookup uses `revision - oldest_revision`
- missing log session returns clean error

### Step 3: SessionManager evidence reads

Add manager-owned read methods over `LogBuffer`.

Implement:
- tail by lines
- range by revision
- context by entry id/revision
- limit enforcement

Tests:
- bounded tail returns expected lines
- range does not exceed requested revision window
- context returns expected before/after lines
- context returns `buffer_exhausted` when anchor revision is gone

### Step 4: Search and highlight

Implement substring search over retained log entries.

Tests:
- search returns matching entries
- highlight offsets match the query
- limit/truncation works
- every returned entry has `entry_id` so client can request context

Regex:
- keep regex optional for MVPv1
- do not ship regex on top of `std::regex`
- if regex is included, prefer RE2 or PCRE2 with match-cost controls
- substring search is sufficient for the first implementation
- regex can remain a later enhancement until the dependency and safety limits are settled

### Step 5: Observation store

Add in-memory ring buffer.

Tests:
- event added on finalized evidence response
- ring evicts oldest events
- list returns newest first or documented order
- identical consecutive tail reads can be deduplicated or rate-limited if they would flood the store

### Step 6: CLI commands

Add `sentrits evidence tail/search/context`.

Tests:
- command returns text
- `--json` returns structured result
- command creates observation event when routed through host
- command propagates `SENTRITS_SESSION_ID` into observation actor fields

### Step 7: Client endpoints

Add HTTP endpoints for:
- observation list
- evidence replay
- direct tail/search for client viewer

Tests:
- JSON schema stable
- replay token resolves to same evidence range when data still exists

### Step 8: iOS viewer

Implement lightweight evidence panel:
- list recent observation cards
- tap opens evidence viewer
- render host-provided highlights

No terminal control behavior should be attached to this view.

## Testing Strategy

### Unit Tests

- log buffer line ingestion from stdout/stderr chunks
- stable entry id generation
- substring match and highlight offsets
- truncation and limits
- retention eviction and dropped counters
- context lookup by revision
- observation ring behavior
- JSON serialization

### Integration Tests

Use managed sessions:
- start a logging session that prints known lines
- call `sentrits evidence search`
- assert evidence result contains expected lines
- assert observation event exists with:
  - expected `actor_session_id`
  - expected source session id
  - expected `operation`
  - expected query
  - non-empty replay token
  - replay token parses to the expected descriptor
- assert replay returns the same bounded view
- assert client replay without `X-Sentrits-Actor-Session` does not create another observation event

### Manual Product Test

1. Start logging session.
2. Start agent session.
3. Agent runs evidence search.
4. iOS receives observation.
5. Tap opens evidence viewer.
6. Human sees same filtered/highlighted lines the agent saw.

## Non-Goals For MVPv1

- perfect auth
- durable audit log
- cross-session writes
- human approvals
- role model
- job dispatch
- info-session ingestion
- live subscriptions
- large-scale indexing

## Success Criteria

MVPv1 is successful when:
- an agent can inspect another managed session through `sentrits evidence`
- evidence reads are bounded and host-filtered
- every read creates an observation event
- the client can open the exact evidence range or filter result
- human and agent see the same content and highlights
