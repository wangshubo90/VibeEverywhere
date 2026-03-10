# Test-Driven Development Policy

VibeEverywhere should be built with a test-driven development mindset.

## Default Rule

For new behavior, write a failing automated test first whenever the seam is accessible at reasonable cost.

This applies to:

- session lifecycle transitions
- PTY output buffering
- reconnect and tail replay logic
- client control arbitration
- controller-owned PTY resize behavior
- API request validation
- event batching and slow-client degradation

## Why This Matters Here

The project has several correctness-heavy areas where regressions will be subtle:

- concurrent PTY ingestion and network delivery
- bounded memory behavior under burst output
- recovery across reconnect and restart
- multi-client authorization and controller transfer
- host-to-remote and remote-to-host control return rules
- platform-facing process and file-watching code

These areas benefit from executable specifications more than ad hoc manual testing.

## Expected Coverage by Layer

## Unit

Use unit tests for pure logic and bounded components:

- ring buffer eviction
- sequence number handling
- delivery state transitions
- snapshot assembly
- session state machine
- git diff/status parsing adapters if introduced

## Integration

Use integration tests for subsystem interaction:

- session manager with provider launch stubs
- PTY manager with test process fixtures
- WebSocket dispatcher with simulated slow clients
- REST snapshot and tail retrieval

## End-to-End

Use end-to-end tests for user-visible behavior:

- create session, attach, receive output, send input
- reconnect and replay tail from sequence boundary
- controller handoff between multiple clients
- host terminal attach and reclaim-control behavior
- degraded-mode behavior under backpressure

## Testability Requirements

Design code so tests can control time, I/O, and process launch behavior:

- inject clocks rather than reading wall time directly in core logic
- hide PTY/platform primitives behind interfaces
- separate pure state transitions from transport code
- support deterministic fake event sources in tests

## Exceptions

Test-first may be deferred only when one of these is true:

- bootstrapping a seam is required before the behavior can be isolated
- platform APIs require a minimal spike to discover the correct abstraction
- a one-off scaffolding change has no practical observable behavior

When that happens:

- note the reason in the change summary
- add the automated test as soon as the seam exists
- avoid leaving behavior covered only by manual verification

## Definition of Done

A feature is not done when code merely compiles. It is done when:

- the intended behavior is covered at the correct test layer
- regressions around adjacent behavior are considered
- the test suite passes under the supported local toolchain
