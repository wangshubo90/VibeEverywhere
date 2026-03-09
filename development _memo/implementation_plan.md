# Implementation Plan

This document turns the initiative docs into an implementable plan for a C++20 codebase.

## Project Goal

Build `vibe-hostd`, a host daemon that manages AI coding CLI sessions and exposes them to remote clients over REST and WebSocket without using remote desktop streaming.

## Delivery Priorities

1. Stable session model and lifecycle
2. PTY process execution and terminal I/O capture
3. Bounded output buffering with replay support
4. REST and WebSocket surfaces for session interaction
5. File and git observation
6. Multi-client control semantics
7. Pairing/auth foundations

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

Acceptance criteria:

- contract tests validate API schema
- attach workflow is functional end-to-end
- controller permissions are enforced

## Phase 5: Workspace Observability

Deliverables:

- file watching
- git state inspection
- event aggregation and throttling

Acceptance criteria:

- file and git updates are visible through session APIs
- high-frequency changes do not flood clients uncontrollably

## Phase 6: Security and Pairing

Deliverables:

- paired device model
- token validation hooks
- session-level authorization checks

Acceptance criteria:

- unauthenticated requests are rejected
- read-only and controller permissions are distinguished

## Cross-Cutting Rules

- Preserve raw terminal bytes.
- Never let client slowness block PTY ingestion.
- Prefer deterministic, injectable subsystems for testability.
- Persist only lightweight recovery data.
- Keep platform-specific behavior behind interfaces from the start.

## Immediate Next Docs to Consult

- [architecture_refined.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/architecture_refined.md)
- [session_runtime_and_pty.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/session_runtime_and_pty.md)
- [api_and_event_schema.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/api_and_event_schema.md)
- [implementation_milestones.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/implementation_milestones.md)
