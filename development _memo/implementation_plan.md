# Implementation Plan

This document turns the initiative docs into an implementable plan for a C++20 codebase.

## Project Goal

Build `vibe-hostd`, a host daemon that acts as a session runtime and supervision/control plane for AI coding CLIs, exposing sessions to remote clients over REST and WebSocket without using remote desktop streaming.

## Delivery Priorities

1. Stable session model and lifecycle
2. PTY process execution and terminal I/O capture
3. Bounded output buffering with replay support
4. REST and WebSocket surfaces for session interaction
5. Multi-client control semantics
6. Runtime observability signals
7. Workspace file and git observability
8. Coarse supervision state and event generation
9. Pairing/auth foundations
10. Lightweight recovery and persisted metadata
11. Thin operational web clients

## Recommended Initial Stack

- C++20 for production runtime
- CMake + Ninja for build
- Clang/LLVM as the main compiler
- GoogleTest or Catch2 for tests
- Asio or Boost.Asio for async networking and timers
- A focused HTTP/WebSocket library chosen for maintainability and low integration risk

The exact library choices should be finalized in the bootstrap phase, but the architecture should avoid deep lock-in to a single networking or JSON dependency.

## Phase 0: Bootstrap

Deliverables:

- root `CMakeLists.txt`
- compiler and warning policy
- third-party dependency strategy
- test framework integration
- initial CI-ready local build and test commands

Acceptance criteria:

- repo configures with CMake + Ninja
- a trivial binary builds under Clang
- a trivial test target runs under `ctest`

## Phase 1: Core Domain Model

Deliverables:

- session identity and metadata types
- session lifecycle state machine
- provider configuration model
- lightweight snapshot model

Acceptance criteria:

- lifecycle transitions are validated by unit tests
- snapshot serialization contract is defined
- provider launch inputs are explicit and testable

## Phase 2: PTY Runtime

Deliverables:

- PTY abstraction
- process launch path for providers
- session runtime object
- input and resize handling

Acceptance criteria:

- test fixture can launch a controllable child process
- session receives output and forwards input correctly
- session exit and error states are observable

## Phase 3: Output Buffering and Delivery

Deliverables:

- bounded session output buffer
- sequence numbering
- tail retrieval API surface
- client delivery state machine

Acceptance criteria:

- slow-client behavior is tested
- bounded memory policy is enforced
- reconnect and recent-tail replay are specified and verified

## Phase 4: Host APIs

Deliverables:

- REST routes for session management and snapshots
- WebSocket subscription model
- event schema for terminal, file, git, and session updates
- attach/replay behavior that works for both local and remote session views

Acceptance criteria:

- contract tests validate API schema
- attach workflow is functional end-to-end
- observer attach and initial replay behavior are verified

## Phase 5: Control Semantics

Deliverables:

- explicit controller ownership in session state
- input and resize rules bound to the active controller
- host-side terminal modeled as the initial attached controller
- controller release and return-to-host behavior

Acceptance criteria:

- only one controller may drive PTY input and resize
- observer clients cannot mutate session state
- controller handoff behavior is covered by tests

## Phase 6: Workspace Observability

Deliverables:

- file watching
- git state inspection
- process-tree observation seam
- event aggregation and throttling
- read-only inspection data model for clients

Acceptance criteria:

- file and git updates are visible through session APIs
- recent file changes are based on real watcher data, not placeholders
- session inventory can surface coarse file/git hints without loading full detail
- high-frequency changes do not flood clients uncontrollably

## Phase 7: Session Inference and Supervision

Deliverables:

- conservative supervision state
- `SessionSignals`-driven attention heuristics
- explicit separation between lifecycle and attention
- `attentionState`, `attentionReason`, and `attentionSince`
- host-wide inventory subscription events
- optional coarse `SessionPhase` seam
- phase inference seam fed by PTY/filesystem/process/resource signals
- attention-oriented session events
- watch-oriented state surfaces for clients

Acceptance criteria:

- architecture supports `SessionPhase` without locking into provider-specific rules too early
- waiting-for-input, clean exit, and file/git/controller changes can drive conservative attention states
- info-level attention decays automatically rather than sticking forever
- intervention rules remain conservative and time-aware
- clients can consume high-level supervision state without re-inferring it locally
- detection remains tunable and conservative
- session inventory can stay live via subscription rather than manual refresh
## Phase 8: Security and Pairing

Deliverables:

- host web app for local approval and configuration
- paired device model
- short-code pairing request flow
- token validation hooks
- session-level authorization checks
- host-generated self-signed TLS identity for HTTPS/WSS

Acceptance criteria:

- unauthenticated requests are rejected
- read-only and controller permissions are distinguished
- a remote client can pair only after local host approval
- paired clients can reconnect without repeating approval

## Phase 9: Recovery and Host Identity Persistence

Deliverables:

- persisted session metadata store
- persisted bounded terminal tail store
- persisted host identity and pairing store

Acceptance criteria:

- daemon restart restores session records and paired devices
- daemon restart does not pretend live PTYs survived
- recent terminal tail remains available after restart

## Cross-Cutting Rules

- Preserve raw terminal bytes.
- Never let client slowness block PTY ingestion.
- Prefer deterministic, injectable subsystems for testability.
- Persist only lightweight recovery data.
- Keep platform-specific behavior behind interfaces from the start.
- Treat the host terminal and remote clients as the same class of daemon-attached session participants.
- Freeze module interfaces before parallel work starts when a phase spans auth, persistence, and network changes.
- Treat `SessionPhase` as an extensibility seam, not a prematurely rigid taxonomy.
- Treat attention as the first production supervisory layer; keep it separate from process lifecycle.
- Prefer improving runtime signal quality before investing in large frontend rewrites.
- Treat file watching and git inspection as both observability inputs and read-only client data surfaces.
- Keep client-facing file and git features read-only until supervision flows are stable.

## Immediate Next Docs to Consult

- [architecture_refined.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/architecture_refined.md)
- [session_attention_inference_v1.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/session_attention_inference_v1.md)
- [session_runtime_and_pty.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/session_runtime_and_pty.md)
- [api_and_event_schema.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/api_and_event_schema.md)
- [implementation_milestones.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/implementation_milestones.md)
