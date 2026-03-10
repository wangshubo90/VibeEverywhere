# Implementation Milestones

This document defines the recommended execution order for building the system.

## Milestone 1: Repo Bootstrap

Scope:

- CMake project skeleton
- warning policy for Clang
- test framework wired into `ctest`
- placeholder executable and test target

Exit criteria:

- `cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++`
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`

## Milestone 2: Core Session Domain

Scope:

- session identifiers and metadata
- state machine
- provider launch spec model
- snapshot metadata model

Exit criteria:

- state-machine tests pass
- serialization contracts exist for snapshot metadata

## Milestone 3: PTY and Process Launch

Scope:

- PTY abstraction
- provider process launch
- input and resize path
- exit handling

Exit criteria:

- integration test runs a fixture command through PTY
- output capture and input injection are verified

## Milestone 4: Output Buffer and Delivery Policy

Scope:

- ring buffer
- sequence tracking
- tail retrieval
- client delivery modes

Exit criteria:

- unit tests cover eviction and replay behavior
- integration tests cover slow-client degradation

## Milestone 5: REST and WebSocket API

Scope:

- session create/list/detail routes
- snapshot and tail endpoints
- WebSocket subscription and event dispatch
- initial attach/replay behavior shared by local and remote clients

Exit criteria:

- end-to-end attach flow works
- API contract tests pass

## Milestone 6: Controller Ownership and Attach Semantics

Scope:

- explicit controller state
- input and resize through the active attached client
- host-following behavior when remote takes control
- return-of-control behavior

Exit criteria:

- one controller at a time is enforced
- PTY resize follows the active controller
- controller handoff is covered by tests

## Milestone 7: File and Git Observability

Scope:

- file watcher integration
- git inspector integration
- event aggregation

Exit criteria:

- workspace changes and git summaries appear in session snapshot and live updates

## Milestone 8: Auth and Control Arbitration

Scope:

- pairing model
- token validation
- controller handoff rules

Exit criteria:

- unauthorized actions are rejected
- multi-client controller behavior is tested

## Milestone 9: Recovery Hardening

Scope:

- lightweight persisted snapshots
- host restart restoration of session metadata
- bounded terminal tail persistence

Exit criteria:

- restart recovery restores exited session records without pretending processes survived

## Ongoing Deliverable Rules

- each milestone should land with tests
- each architectural deviation should update the relevant markdown
- avoid expanding scope into IDE features or remote file editing
- avoid introducing host-only session semantics that bypass daemon control flow
