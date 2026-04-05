# Session Terminal Multiplexer And Semantic Runtime

Implementation note:

- this document is the broader target-state direction
- the implementation-facing starting point is now:
  - [session_terminal_multiplexer_v1.md](Sentrits-Core/development_memo/future/session_terminal_multiplexer_v1.md)
  - [session_terminal_multiplexer_wire_compatibility.md](Sentrits-Core/development_memo/future/session_terminal_multiplexer_wire_compatibility.md)
- v1 intentionally keeps the current API compatible and defers semantic monitor work from the critical path

## Purpose

This document defines the major runtime refactor needed to support:

- cross-client continuity for one live session
- many observers with independent terminal views
- one active stdin writer instead of one broad "controller"
- richer semantic monitoring for humans and agent observers
- session inventory and graph views built from structured state rather than raw terminal bytes alone

This is a target-state design. It does not describe the current implementation.

## Why This Refactor Exists

The current runtime has one PTY per session and forwards the same terminal byte stream to all attached clients.

That model is good enough for:

- one active controller
- passive observers that tolerate imperfect rendering
- simple tail replay

It breaks down for the actual product direction:

- leave a session on one client and continue it on another
- different observer sizes on web, iOS, and host terminals
- many observers, but only one active writer
- agent observers that care about semantics, diffs, snapshots, and recent state more than raw ANSI

The current known flaw is structural:

- shared PTY resize corrupts peer views
- first-frame reconstruction is weak
- control handoff depends on repaint behavior of the program inside the PTY
- raw terminal bytes are not a sufficient observation model for inventory, previews, or agent context

## Core Design Shift

### Old mental model

- one controller
- many observers
- shared PTY size
- raw PTY bytes as the main live artifact

### New mental model

- one active stdin owner
- many observers
- one executing PTY per session
- multiplexed terminal state derived from PTY output
- semantic session state layered on top of transport state

The exclusive privilege is not "full control". The exclusive privilege is:

- write to stdin of the live PTY

Everything else should be modeled separately:

- observe terminal output
- hold a local rendered view
- inspect snapshots
- read semantic summaries
- request attention
- stop session or mutate metadata under separate policy

## Goals

- Preserve one real PTY-backed process per session.
- Preserve many observers.
- Preserve one active stdin owner at a time.
- Let a session be resumed cleanly on another client with useful recent history.
- Stop observer size from mutating every other observer's rendered view.
- Expose structured session state for inventory, previews, graph views, and agents.
- Keep raw PTY bytes as execution truth, but not as the only client-facing artifact.

## Non-Goals

- Full tmux feature parity.
- Arbitrary multi-writer terminal input.
- Perfect infinite terminal history.
- Replacing provider-native semantics with runtime guesses.
- Solving every TUI edge case in the first version.

## Design Principles

### 1. One PTY, many views

There is still one executing PTY per session.

The runtime should no longer treat "what one client terminal currently sees" as the canonical representation for all clients.

### 2. One active stdin owner

Only one principal may inject PTY input at a time.

This is the actual exclusive capability.

### 3. View state is per observer

Each attached observer should have an independent rendered terminal view or viewport state.

View state should not be equivalent to PTY state.

### 4. Semantics are additive, not authoritative

Raw PTY execution remains the source of truth.

Semantic extraction exists to improve:

- continuity
- previews
- searchability
- agent comprehension
- supervision

### 5. Snapshot plus replay is the continuity model

A client should not need a fresh repaint from the process to become useful.

A client should be able to:

- fetch a current session snapshot
- fetch bounded replay/history
- subscribe to live updates

## Target Runtime Layers

```text
Provider Process
  -> PTY
  -> SessionRuntime
  -> TerminalMultiplexer
       -> canonical event stream
       -> per-view terminal render state
       -> bounded terminal replay
  -> SemanticMonitor
       -> summaries
       -> diffs
       -> file change state
       -> inventory preview state
  -> SessionStore
       -> persisted metadata, replay windows, semantic state
  -> API / WebSocket Layer
       -> observer APIs
       -> stdin-owner APIs
       -> inventory / graph / agent APIs
```

## Main Components

### SessionRuntime

Responsibilities:

- own the real PTY and provider process
- poll PTY output
- accept stdin from the active stdin owner
- maintain lifecycle state
- emit raw output chunks with sequence numbers

It should not own per-observer rendering.

### TerminalMultiplexer

Responsibilities:

- consume ordered PTY output
- maintain terminal replay history
- maintain one or more renderable terminal states
- support observer attach without forcing a process repaint
- support per-observer terminal dimensions

This component is the runtime-side replacement for raw PTY fanout.

### TerminalView

A `TerminalView` is an attached observer-specific render model.

Recommended first fields:

- `view_id`
- `session_id`
- `principal_id`
- `cols`
- `rows`
- `delivered_sequence`
- `last_rendered_revision`
- `connected_at`
- `last_seen_at`

Each view holds enough state to derive the text and visual screen for that observer.

### SemanticMonitor

Responsibilities:

- consume PTY events, screen state, workspace change signals, and git state
- produce structured semantic summaries
- expose preview-ready and agent-ready state

This should combine:

- recent rendered terminal excerpts
- file changes
- git status
- recent commands when inferable
- prompt / waiting / progress hints
- quick summaries suitable for inventory and graph nodes

### SessionStore

Responsibilities:

- persist bounded replay windows
- persist session metadata
- persist semantic state
- persist parent/child and observation relationships

## Terminal State Model

The runtime should explicitly separate three kinds of state.

### 1. Transport state

Examples:

- session status
- PTY alive / exited / error
- current output sequence
- active stdin owner

### 2. Render state

Examples:

- current screen text
- cursor position
- scrollback ring
- visible title
- recent rendered revisions
- per-view dimensions

### 3. Semantic state

Examples:

- recent summary
- recent file changes
- git branch / dirty state
- recent task title / inferred phase
- recent notable warnings or failures
- preview text for inventory

## Stdin Ownership Model

Replace the current broad controller model with `stdin ownership`.

### Exclusive capability

- `session.stdin.write`

Only one principal may hold it at a time.

### Shared capabilities

- observe output
- fetch snapshots
- fetch replay
- subscribe to session events
- inspect semantic summaries

### Separately governed capabilities

- stop session
- rename or retag session
- create child session
- approve pairings
- mutate host config

Those should not be implicitly bundled into stdin ownership.

## Observer Model

All attached clients are observers by default.

An observer may:

- receive live rendered output
- receive semantic updates
- fetch replay
- fetch current screen snapshot
- request stdin ownership

An observer may not:

- send terminal input without stdin ownership

## Agent Observer Model

Agent observers should not depend on raw terminal bytes as their primary context source.

They should consume:

- session summary
- current semantic snapshot
- recent file changes
- git summary
- recent terminal excerpt
- current task preview
- parent / child / observation graph metadata

That allows agent observers to understand a session without being fragile to ANSI repaint noise.

## History Model

The continuity requirement is:

> A user can leave a session on one client and continue it on another without losing useful context.

That requires bounded but meaningful history.

### Recommended retained history tiers

#### Render replay

- bounded terminal output ring buffer
- enough to reconstruct recent screen changes

#### Current screen snapshot

- current rendered text state
- cursor and metadata

#### Semantic replay

- recent summaries
- recent file changes
- recent git changes
- notable prompts / errors / waiting states

### Suggested retention policy

First version should use explicit bounded retention such as:

- recent PTY output: last N MB or N lines
- recent rendered revisions: last N snapshots
- recent semantic events: last N items
- recent file changes: last N files or N minutes

The exact values should be tuned by profiling and real usage.

## API Direction

The runtime should expose distinct read models.

### Observer APIs

- `GET /sessions/:id/snapshot`
- `GET /sessions/:id/replay`
- `GET /sessions/:id/semantic`
- observer WebSocket for:
  - rendered output updates
  - semantic updates
  - session metadata updates

### Stdin owner APIs

- request stdin ownership
- release stdin ownership
- send terminal input
- optionally stop session under separate policy

### Inventory / graph APIs

- host overview snapshot
- semantic preview state per session
- relationship metadata:
  - parent session
  - child sessions
  - current observers

## Relationship And Graph Metadata

Session graph state should remain host-qualified.

Recommended direct metadata:

- `host_id`
- `session_id`
- `parent_host_id`
- `parent_session_id`
- `created_child_refs`
- `observer_refs`

The runtime should store direct edges only.

Clients should assemble graph views.

## Terminal Multiplexer Strategy

There are two viable internal strategies.

### Option A: one canonical emulator plus per-view projection

Pros:

- cheaper
- simpler first implementation

Cons:

- weaker support for truly independent client dimensions
- harder to make every observer view accurate

### Option B: per-view emulator state fed from one PTY stream

Pros:

- each observer gets a true own view
- best match for web, iOS, and host terminals with different dimensions

Cons:

- higher memory and CPU cost
- more bookkeeping

Recommended direction:

- start with a canonical render state plus attach snapshot and bounded replay
- move toward per-view emulation if canonical projection is not sufficient

If the product requirement remains "true own views", Option B is the likely end state.

## Semantics Monitoring Scope

The semantic layer should care about more than terminal bytes.

Useful semantic artifacts include:

- current summary text
- recent file changes
- git status summary
- recent prompt / confirmation / waiting hints
- inventory preview text
- recent terminal tail excerpt
- session attention state
- quick view diffs and snapshots

This layer exists primarily for:

- inventory
- graph nodes
- quick previews
- agent observers
- continuity across client switches

## Migration Plan

### Phase 1: capability cleanup

- standardize on `stdin owner` terminology internally
- keep current control model behaviorally compatible
- preserve existing controller sockets as the write-ownership path

### Phase 2: observer separation

- add explicit read-only observe path everywhere
- keep local host attach from being the default observer mechanism
- stop observer flows from mutating PTY size

### Phase 3: snapshot and replay

- add canonical current screen snapshot
- add bounded replay API
- improve first-frame attach behavior without process repaint

### Phase 4: semantic state

- add semantic monitor outputs to session state
- expose semantic preview APIs
- update inventory and graph UIs to consume semantic summaries

### Phase 5: terminal multiplexer

- introduce runtime-side terminal-state multiplexing
- reduce or eliminate direct dependence on shared raw PTY fanout for normal clients

### Phase 6: agent observer APIs

- provide structured agent observation surfaces
- keep agent context centered on semantic and snapshot state, not raw ANSI

## Compatibility Strategy

The refactor should preserve:

- one PTY per session
- one active stdin owner
- current remote controller privilege boundaries
- current pairing and host identity model

The refactor should deliberately break:

- the assumption that `attach` means "resize the shared PTY for everyone"
- the assumption that raw PTY stream is the best observer contract

## Open Questions

- Should first implementation use canonical render state only, or go directly to per-view emulators?
- What retention limits are acceptable on desktop, laptop, and mobile-hosted environments?
- Which semantic outputs are persisted, and which are transient?
- Should stdin ownership auto-expire on idle, disconnect, or explicit release only?
- Which non-stdin mutations should still require elevated local or paired-device authority?

## Recommended First Concrete Deliverables

- explicit read-only observe paths on all clients and CLI
- `stdin owner` terminology in runtime and API docs
- current screen snapshot API
- bounded replay API
- semantic session preview object
- inventory and graph clients switched to semantic previews instead of raw tail only

## Summary

The next runtime architecture should be built around this rule:

- one real PTY per session
- one active stdin owner
- many observers with independent terminal views
- bounded continuity history
- semantic state as a first-class runtime product surface

That architecture preserves the current session model, keeps privilege boundaries understandable, and supports the product goal of leaving a live session on one client and continuing it on another without losing meaningful context.
