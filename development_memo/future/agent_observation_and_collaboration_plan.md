# Agent Observation and Inter-Session Collaboration Plan

## Purpose

This document merges the ideas in `development_memo/thoughts/logging.md` and `development_memo/thoughts/messaging.md` into one implementation plan.

The shared product idea is:

Sentrits should make both **cross-session actions** and **cross-session observation** explicit, permissioned, and visible to the human.

The product frame is a remote debugging control plane:

Developers can attach to live development sessions, inspect logs and runtime signals, and intervene from anywhere while the host keeps authoritative identity, evidence, and permission state.

That means two first-class flows:

1. An agent session can request work or actions involving another session.
2. An agent session can read bounded data from another session or from host-owned info streams.

Both flows should go through Sentrits APIs, not direct uncontrolled terminal behavior.

## Validation of the Core Ideas

### Idea 1: Session-to-session messaging through host APIs with approval

Validated, with one refinement.

The useful abstraction is not generic "messaging". It is **request routing**.

The safe form is:
- Session A creates a request targeting Session B.
- Sentrits authenticates the caller.
- Sentrits checks capability and risk policy.
- Sentrits either executes, queues, or asks the human for approval.
- Sentrits records the action in an audit trail.

This is stronger than direct stdin injection because it:
- preserves session boundaries
- gives the client a chance to approve
- creates replayable audit state
- lets Sentrits distinguish low-risk observation from high-risk control

Recommendation:
- Make `Request` the primary cross-session object.
- Treat direct input injection as a later, explicitly high-risk request type.

### Idea 2: Agent reads data from other sessions through Sentrits APIs, while client shows what it read

Strongly validated.

This is one of the best product directions in the drafts.

The important shift is:
- agents do not "just inspect logs"
- agents inspect **evidence through Sentrits**
- Sentrits emits observation events
- the client can show exactly what evidence the agent consumed

This is valuable because it solves a real trust problem:
- humans cannot normally tell what an agent actually looked at
- evidence-trail APIs make agent observation externally visible without exposing chain-of-thought

Recommendation:
- Make `ObservationEvent` a first-class runtime object.
- Emit it automatically from host APIs such as search, tail, open range, and filtered read.
- Build the client UX around "what the agent read" rather than "what the agent thought".

### Shared idea: PID, UID, GID, and session-managed identity

Validated, with an important constraint.

PID/UID/GID serve two related but separate purposes:
- identity: which managed session, actor, process, or child process is doing something
- authentication: whether the caller is allowed to claim that identity

They should be treated as **authentication evidence**, not the whole identity model.

Recommended identity model:
- Sentrits-managed `session_id`
- Sentrits-issued `actor_id`
- Sentrits-owned process identity for the root PTY process and known child processes
- peer credentials from Unix domain socket (`pid`, `uid`, `gid`)
- session-scoped capability token injected into managed environments
- optional process-tree validation when practical

Why this matters:
- PID alone is too weak
- UID/GID alone is too broad
- token alone is not enough if it escapes the session boundary

So the right model is:
- peer credentials + session boundary mapping + capability token + permission policy

The client-facing reason this matters is not abstract security. The host needs to know which session and which spawned child process performed an action so it can push useful facts to the client:
- this agent searched this log
- this child process tailed this runtime stream
- this session requested permission to act on another session
- this output panel is what the agent is currently inspecting

## Product Boundary

This should not become a general autonomous agent platform.

It should be a **host-side supervision and collaboration layer** for managed sessions.

Sentrits should own:
- session identity
- session-to-session requests
- evidence reads
- approval prompts
- audit trail
- client-visible observation history

Sentrits should not try to own:
- arbitrary multi-agent planning DAGs
- unrestricted inter-agent chat
- hidden background autonomy
- open-ended cross-session input by default

## Unified Runtime Model

The two drafts should converge on one runtime model with five object families.

### 1. Session

Existing long-lived managed CLI/workspace session.

Needed additions:
- role metadata
- capability set
- actor identity
- approval policy hooks

### 2. Request

A cross-session or host-directed intent created by an actor.

Examples:
- assign job
- ask reviewer to inspect a diff
- ask tester to run a test
- ask to read another session's output range
- ask to inject input into another session
- ask for human approval

### 3. ObservationEvent

A record of evidence access.

Examples:
- searched session output for a pattern
- opened a bounded context range
- tailed recent output
- subscribed to filtered stream
- viewed highlighted lines

This is the key shared primitive for both humans and agents.

### 4. ApprovalRequest

Human decision point for high-risk actions.

Examples:
- input injection
- interruption
- destructive job dispatch
- sensitive data access
- broad cross-session observation if policy requires it

### 5. AuditEvent

Immutable runtime log for:
- requests
- approvals
- deliveries
- observation events
- denials
- policy decisions

## Core API Surfaces

The two drafts currently split APIs into "messaging" and "logs". For implementation, this should eventually become three host surfaces.

For the first MVP, only Surface B and the observation-list part of Surface C are in scope.

### Surface A: Collaboration API

Used by managed sessions to request actions.

Examples:
- `requests.create(...)`
- `requests.get(...)`
- `jobs.list(...)`
- `jobs.accept(...)`
- `jobs.complete(...)`
- `approvals.request_human_help(...)`

Status:
- future phase

### Surface B: Evidence API

Used by managed sessions and clients to read bounded data.

Examples:
- `evidence.list_sources(...)`
- `evidence.search(...)`
- `evidence.tail(...)`
- `evidence.context(...)`
- `evidence.subscribe(...)`
- `evidence.bookmark(...)`

This should cover both:
- host-owned info sessions
- managed session output views

MVP scope:
- managed session output views only

### Surface C: Observation/Audit API

Used by clients to render what happened.

Examples:
- `observations.list(...)`
- `requests.list(...)`
- `approvals.list_pending(...)`
- `audit.list(...)`

MVP scope:
- `observations.list(...)`
- event stream or polling for recent evidence reads

## Important Design Decision: Treat Session Output as an Evidence Source

To unify the two drafts, Sentrits should treat ordinary session output as just another readable evidence surface.

That means an agent should be able to read:
- info sessions such as logs and telemetry
- other session output via bounded APIs
- filtered/highlighted projections of either

This gives one consistent model:
- `EvidenceSourceKind = session_output | info_session`

That is simpler than building one system for logs and another for sessions.

## Client UX Recommendation

The client should not expose raw runtime complexity.

### MVP user-visible surfaces

1. `Agent evidence`
- human sees what the agent searched, opened, or tailed
- each item is clickable and reproducible

2. `Highlights / filtered evidence view`
- when an agent reads a bounded range or filter result, the client can show that same range or highlight set
- this makes the agent's evidence visible and reviewable

### Future user-visible surfaces

1. `Pending approvals`
- human sees what an agent wants to do
- approve or reject

2. `Session relationships`
- human sees who is reading whom
- human sees which session requested work from which other session

### Avoid

Do not make the client render full audit internals by default.
Use compact cards and drill-down.

## Security and Safety Model

### Authentication

For the local gateway, authenticate callers using:
- Unix domain socket peer credentials
- session token injected into the managed environment
- session ownership mapping maintained by Sentrits

### Authorization

Use capability and risk policy on every action.

Low-risk examples:
- list evidence sources
- read bounded output range
- search logs
- request review
- request testing

High-risk examples:
- inject input into another session
- interrupt another session
- terminate another session
- perform destructive cross-session commands
- broad or persistent data access outside policy

### Approval policy

Default policy should be conservative:
- reading bounded output from same workspace: usually allow
- filtered log access: usually allow
- job dispatch to role-appropriate session: allow if capability exists
- stdin injection or interruption: require approval
- destructive requests: require approval

## MVP Feature Hierarchy

This hierarchy keeps the first build focused on the remote debugging control plane instead of starting with broad collaboration.

The ranked MVP is:

1. Read from managed sessions.
2. Show the client what the agent read.
3. Add host-side filtered/highlighted views.
4. Later, generalize the same model to info sessions, requests, approvals, and intervention.

The first build should be useful even if authentication is minimal. It should prove the product loop before hardening the trust model.

### Level 0: Managed session identity

Goal:
- Sentrits can identify a managed session as the actor that initiated an evidence read.

Features:
- assign `session_id` and `actor_id` to every managed session
- inject `SENTRITS_SESSION_ID`, `SENTRITS_ACTOR_ID`, and `SENTRITS_LOCAL_API`
- optionally inject `SENTRITS_TOOL_TOKEN`, but do not block the first prototype on full auth
- record caller `pid`, `uid`, `gid`, and resolved `session_id` when available
- keep a clear path to later process-tree validation

This level is mostly identity in the first cut. Strong authentication can follow after the read path proves useful.

### Level 1: Session output as an evidence source

Goal:
- A managed agent can read bounded data from another managed session or from its own session through Sentrits APIs.

Features:
- expose managed session output as `EvidenceSourceKind::session_output`
- support bounded tail, range, and context reads
- support simple host-side substring search first
- support regex after the substring path is stable
- return revision or sequence ranges for every read
- avoid raw unbounded log dumps

This gives agents a structured way to inspect data without making the client or agent scrape terminal history.

Initial user workflow:
- user or agent starts a logging session, for example `sentrits session start somelogging`
- the agent calls a Sentrits evidence command/API to read that session
- the host performs the read/search/filter work
- the client receives an observation notification

### Level 2: Observable observation

Goal:
- When an agent reads evidence, the user can see what it read.

Features:
- create `ObservationEvent` for every evidence API read
- include actor identity, source session, query/filter, range, timestamp, and result summary
- expose recent observation events to clients
- allow client to open the same range or filtered result
- push active observation updates to client while a session is running

This is the first user-visible differentiator: the host can actively tell the client what the agent is inspecting.

Client behavior:
- receive a notification/card such as `Agent searched logging-session for "error|timeout"`
- tap to open a lightweight evidence viewer
- show filtered/highlighted lines, not a terminal control surface
- allow regex/preset/highlight metadata to come from the host result
- keep iOS responsible for lightweight rendering only

### Level 3: Info sessions for host-owned streams

Goal:
- Treat console logs, runtime logs, build logs, and plain stdout streams as first-class evidence sources.

Features:
- create `InfoSession` for host-started streams such as console logging, build output, or runtime stdout
- assign revisions to text entries
- support tail, range, context, and search through the same Evidence API
- show highlights and filtered results in clients

This is where plain stdout and shell-based console logging become inspectable by both humans and agents.

This is not required for the first MVP if a regular managed session can already serve as the logging source.

### Level 4: Low-risk session-to-session requests

Goal:
- Sessions can ask other sessions to do bounded, explicit work without direct control.

Features:
- `Request` object with source session, target session, type, status, title, and payload
- low-risk request types first:
  - `RequestObservation`
  - `RequestReview`
  - `RequestTest`
  - `AssignJob`
- request status visible to clients
- audit events for create, delivery, completion, failure, and cancellation

This creates collaboration without introducing cross-session input injection yet.

### Level 5: Human approval workflow

Goal:
- Risky actions become explicit client approval events.

Features:
- `ApprovalRequest`
- risk level and reason
- pending approval feed for clients
- approve/reject API
- policy engine that decides allow, deny, or require approval

This should exist before any cross-session control action ships.

### Level 6: High-risk intervention

Goal:
- Agents can request intervention in another session, but only through approval.

Features:
- request input injection
- request interrupt
- request terminate
- request control handoff
- mandatory approval and audit trail

This is intentionally last. It is powerful, but it depends on the identity, evidence, observation, and approval layers being trustworthy first.

## MVP Cut

The first implementation slice should include only the managed-session reading loop from Levels 0 through 2.

Deliverable:
- a managed session calls a local Sentrits API or CLI wrapper to search or read another session's bounded output
- Sentrits identifies the caller as a managed session when possible
- Sentrits returns bounded evidence
- Sentrits records an `ObservationEvent`
- connected clients receive a notification and can open the same evidence range in a lightweight viewer

Explicitly out of the first MVP:
- strong authentication
- process-tree ancestry validation
- generic info-session ingestion
- subscriptions
- request routing
- approvals
- input injection
- interrupt/terminate/control handoff

The point of the first MVP is to prove: **agent reads managed-session output through Sentrits, and the user can see exactly what was read**.

That is enough to validate the core product loop before adding jobs, approvals, or direct intervention.

## Recommended Implementation Plan

This should be Core-first.

### Phase 0: Unify terminology

Deliverables:
- define `EvidenceSourceKind`
- define `ObservationEvent`
- define `EvidenceReadKind`
- define bounded evidence result schema with source id, sequence range, matched lines, and highlight ranges
- define which existing session output APIs can be lifted into evidence APIs
- defer `Request`, `RequestType`, `ApprovalRequest`, and `AuditEvent` until the read-only MVP works

Reason:
- keep the first model centered on reading managed sessions
- avoid building one API family for logs and another for sessions later

### Phase 1: Managed-session gateway and caller identity

Deliverables:
- separate Unix domain socket gateway for managed-session calls
- do not reuse the controller socket handler stack
- peer credential extraction when available (`pid`, `uid`, `gid`)
- injected environment variables:
  - `SENTRITS_SESSION_ID`
  - `SENTRITS_ACTOR_ID`
  - `SENTRITS_LOCAL_API`
  - `SENTRITS_TOOL_TOKEN` eventually
- first cut may accept session id + local gateway call without full token hardening
- define token lifecycle even if enforcement lands later:
  - generated at session creation
  - stored with the session entry
  - injected before PTY launch
  - invalidated at session exit

Reason:
- a managed session is a lower-trust caller than a paired controller
- keeping this surface separate makes the boundary explicit

### Phase 2: Read-only evidence API

Deliverables:
- list managed-session evidence sources
- read bounded tail, range, and context from session output
- search/filter on host with substring first, regex later
- emit `ObservationEvent` automatically
- route cross-session reads through `SessionManager`, not direct runtime pointers
- use `SessionOutputBuffer::Tail()` and `SliceFromSequence()` behind a manager method
- start with pull-based reads only

Reason:
- this is the lowest-risk, highest-value first feature
- it supports your "show what the agent reads" goal immediately

### Phase 3: Client-visible evidence trail

Deliverables:
- observation event stream/list endpoint
- client panel for `Agent evidence`
- clickable replay into exact log/session ranges
- highlight/filtered views using host-side projections
- lightweight iOS viewer, not a terminal focus view
- host performs filtering, regex, and heavy projection work

Reason:
- without client visibility, the evidence model is incomplete

### Phase 3.5: Agent-facing CLI wrapper

Deliverables:
- CLI surface for managed sessions to call the gateway, for example:
  - `sentrits evidence list`
  - `sentrits evidence tail --session <id> --bytes 4096`
  - `sentrits evidence search --session <id> --pattern error`
  - `sentrits evidence context --session <id> --sequence <n> --before 80 --after 120`
- grep-like aliases can be added after the native commands work, for example:
  - `sentrits evidence grep --session <id> <pattern>`
  - `sentrits evidence find --session <id> --name <text>`
- CLI reads `SENTRITS_LOCAL_API`, `SENTRITS_SESSION_ID`, and optional token from environment
- output is compact JSON or text suitable for coding agents

Reason:
- Claude, Codex, Gemini, and shell agents can use a CLI tool immediately
- the socket protocol stays internal
- future skills can wrap this CLI instead of scraping terminal output
- Unix-language compatibility is useful, but it should translate into the same Evidence API so the client still sees the observation event

### Phase 4: Request object and low-risk cross-session collaboration

Deliverables:
- `Request` runtime object
- request status lifecycle
- allow low-risk request types first:
  - assign job
  - request review
  - request test
  - request observation
- audit trail for create/deliver/complete/fail

Reason:
- gives controlled inter-session communication without needing risky control handoff yet

### Phase 5: Approval engine

Deliverables:
- `ApprovalRequest`
- pending approval feed for clients
- approve/reject API
- policy engine with risk levels

Reason:
- required before any high-risk cross-session action

### Phase 6: High-risk session actions

Deliverables:
- request input injection
- request interrupt
- request terminate
- full audit logging
- mandatory approval

Reason:
- this should come last because it is the most dangerous feature family

## Recommended MVP Slice

The best MVP is not messaging first. It is:

1. local gateway identity
2. read-only evidence API
3. observation events visible in client

Why:
- lower risk
- immediately useful
- strengthens trust in agents
- reuses existing session/log infrastructure
- gives the human new visibility before enabling stronger cross-session control

Then add:
- request objects for job/review/test routing
- approvals
- finally high-risk control requests

## Concrete Risks and Mitigations

### Risk: hidden data access by agents
Mitigation:
- every evidence API call emits `ObservationEvent`
- clients can review the exact ranges and filters

### Risk: spoofed local caller
Mitigation:
- do not trust PID alone
- require peer credential + token + session mapping

### Risk: feature scope explodes into a swarm framework
Mitigation:
- keep request types fixed and explicit
- no arbitrary free-form autonomous routing in MVP

### Risk: mobile client becomes overloaded
Mitigation:
- host does search/filter/projection
- client requests bounded, decision-ready slices

### Risk: user cannot tell difference between read and act
Mitigation:
- separate UI surfaces:
  - `Agent evidence`
  - `Pending approvals`
  - `Requests / jobs`

## Immediate Next Steps

1. Add this merged model to the architecture docs.
2. Define `EvidenceSourceKind`, `ObservationEvent`, and `RequestType` in Core headers.
3. Choose one small end-to-end MVP path:
   - agent session calls local API to search another session's output
   - Sentrits returns bounded results
   - Sentrits records `ObservationEvent`
   - client can list that event
4. After that works, add low-risk `RequestObservation` and `AssignJob`.
5. Leave stdin injection and interruption for later.

## Bottom Line

The two drafts are directionally correct and mutually reinforcing.

The right strategic framing is:
- **reading evidence** and **requesting action** are two sides of the same supervised host API
- `pid`, `uid`, `gid` are useful authentication signals but must be combined with session-managed identity
- the first product win is not arbitrary inter-agent messaging
- the first product win is **auditable evidence access plus visible human approval for risky cross-session actions**
