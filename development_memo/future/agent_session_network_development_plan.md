# Agent Session Network Development Plan

This document consolidates the draft thoughts under `development_memo/thoughts/` into a development-facing plan.

It is narrower than the broad future memos and more concrete than the raw thought notes. The goal is to define a real implementation path for turning Sentrits from a PTY/session runtime into a better substrate for:

- human supervision with more granular session state
- agent observation with useful structured data
- future agent-to-agent interaction across the Sentrits session network

It does **not** assume that raw terminal output is already a sufficient machine interface.

## Problem Statement

Sentrits already supports a network of real running CLI sessions across devices.

The next problem is not transport. The next problem is representation.

Two constraints dominate this work:

1. AI coding CLIs are not request/response services

- they are long-running terminal processes
- they do not emit a clean start/end signal for each subtask
- they often loop through think/edit/run/wait states indefinitely

2. terminal output is unstructured

- PTY bytes are the execution truth
- PTY bytes are not, by themselves, a good product surface for supervision or agent consumption
- raw ANSI is too fragile to be the only basis for human monitoring or agent interaction

So the work ahead is to build a better runtime-owned representation layer on top of live PTY execution, without pretending the PTY stream is already a clean protocol.

## Product Goal

The target outcome is:

- humans can monitor a live coding session with more granularity than just `running` or `stopped`
- agents can fetch useful structured state instead of scraping raw terminal bytes
- another coding CLI can interact with a Sentrits-managed session through stable APIs rather than through brittle PTY imitation alone

This is the path from:

- "persistent session runtime"

to:

- "agent session network with usable supervision and interaction surfaces"

## Current Reality vs Next-Phase Target

### Current Reality

Today, Sentrits already has:

- real daemon-managed PTY sessions
- cross-device observe/control
- canonical terminal snapshot/bootstrap support
- session inventory with lifecycle, controller, and coarse attention signals

But the runtime still leans on relatively coarse session representation.

What is missing is not basic transport. What is missing is a more legible runtime-owned representation for:

- humans supervising sessions
- agents consuming useful structured state

### End Of The Next Phase

By the end of the next practical phase, Sentrits should have:

- canonical terminal state as a stable internal product surface
- explicit interaction classification
- more readable session inventory and node summaries for humans
- the first semantic preview payloads
- additive read-only observation surfaces that are better suited for agent consumption than raw PTY tail text

The goal is not full agent mesh orchestration yet.

The goal is a runtime that can describe a live session better than "running terminal with recent bytes."

## Design Principles

### 1. PTY execution remains truth

The live CLI process in a PTY remains the execution source of truth.

We are not replacing it with a provider-specific API layer.

### 2. Semantic extraction is additive

Semantic and interaction layers should enrich runtime state, not redefine it.

They produce hints, summaries, and machine-usable views on top of:

- PTY output
- terminal state
- filesystem signals
- git state
- process and lifecycle signals

### 3. Terminal state comes before semantics

We should not try to build a useful semantic layer directly from raw ANSI chunks.

The correct order is:

```text
PTY bytes
  -> canonical terminal state
  -> interaction classification
  -> semantic extraction
  -> human/agent-facing structured views
```

### 4. Human monitoring and agent consumption are related, not identical

Humans need:

- understandable state
- attention signals
- progress and waiting hints
- file/git/task summaries

Agents need:

- stable fetchable state
- machine-usable projections
- lower-noise interaction points

These should share the same runtime-owned data model where possible, but they do not need identical payloads.

### 5. Agent interaction should not start as "just another PTY writer"

The current exclusive controller model is still correct for the real PTY.

But future agent-to-agent interaction should not force every automation case to masquerade as an interactive terminal user.

We should eventually expose a higher-level interaction API for agents.

## Consolidated Architecture

The relevant future architecture should be understood as five layers:

```text
live CLI in PTY
  -> terminal state layer
  -> interaction classification layer
  -> semantic extraction layer
  -> human supervision view + agent observation view
  -> future agent interaction API
```

### Layer 1: Terminal State Layer

Purpose:

- convert PTY byte streams into canonical terminal state

Responsibilities:

- ANSI parsing
- cursor and screen state
- main/alternate screen handling
- bounded scrollback
- revisioned snapshots and diffs
- viewport/bootstrap generation for clients

This is the foundation for everything else.

Primary related docs:

- `future/session_terminal_multiplexer_v1.md`
- `future/session_terminal_multiplexer_and_semantic_runtime.md`
- `thoughts/terminal_model.md`

### Layer 2: Interaction Classification Layer

Purpose:

- classify how a PTY-backed session is behaving without semantic interpretation

Examples:

- quick command completed
- running non-interactive task
- interactive fullscreen program
- interactive line-mode program

Inputs:

- process liveness
- foreground process group
- termios mode
- ANSI redraw behavior
- output timing and shape

This layer is useful because "what kind of interaction surface is this right now?" is often needed before deeper semantics.

Primary related draft:

- `thoughts/interaction.md`

### Layer 3: Semantic Extraction Layer

Purpose:

- extract weak but useful semantic hints from terminal state and session signals

Examples:

- likely waiting for input
- visible status line / bottom bar
- likely build/test progress
- likely prompt / approval moment
- visible error region
- stable preview summary

Important rule:

- semantic outputs are hints, not execution truth

Primary related docs:

- `future/pty_semantic_extractor.md`
- `future/session_signal_map.md`

### Layer 4: Supervision Views

Purpose:

- expose useful state for humans and agents without requiring raw ANSI scraping

This splits into two related views:

1. human supervision view
- attention state
- interaction kind
- recent file/git changes
- semantic preview
- task/wait/progress hints

2. agent observation view
- structured terminal summary
- bounded rendered text window
- recent semantic events
- interaction and attention state
- relevant file/git/task signals

### Layer 5: Agent Interaction API

Purpose:

- let another coding CLI or agent interact with a session through a stable runtime API

This is future-facing, but it should be designed now so the earlier layers move toward it cleanly.

The key idea:

- agents should be able to **fetch** useful state
- and eventually **act** through explicit runtime APIs
- without every interaction being reduced to raw PTY keyboard emulation

## Core Data Model

The consolidated runtime representation should eventually separate these kinds of state explicitly.

### 1. Execution State

Examples:

- lifecycle status
- root process state
- controller owner
- current PTY size
- current sequence / revision

### 2. Terminal Render State

Examples:

- current screen
- current viewport bootstrap
- bounded scrollback
- cursor state
- revisioned diffs

### 3. Interaction State

Examples:

- interaction kind
- alt-screen active
- line-mode vs fullscreen
- redraw-heavy vs batch-like output

### 4. Semantic State

Examples:

- likely waiting state
- likely progress state
- bottom status summary
- visible prompt summary
- error hints
- semantic preview object

### 5. Session Node Summary

Examples:

- session title and provider
- workspace root
- last activity
- attention state
- current interaction kind
- semantic preview
- recent file/git indicators

This "session node summary" is the right long-term object for inventory, graph views, watch workflows, and future orchestration logic.

## Human Monitoring Scope

The first practical product win from this work is better human supervision.

The runtime should eventually expose more granular monitoring than:

- `running`
- `quiet`
- `stopped`

Useful next-level human-visible states:

- active editing
- running task
- likely waiting for input
- fullscreen interactive mode
- recent error burst
- controller changed recently
- workspace changed recently

This does **not** require pretending the runtime fully understands the CLI. It only requires that the runtime becomes better at surfacing useful session evidence.

## Agent Observation Scope

The first practical agent-facing win is a stable observation surface.

Agents should be able to fetch:

- current session node summary
- rendered text window
- bounded recent rendered history
- interaction kind
- attention state
- semantic preview object
- recent file/git/task hints

This is much better than:

- raw tail bytes only
- terminal scraping in every client

## Agent Interaction Scope

Future agent interaction should be introduced in stages.

### Stage A: Better observation only

Expose stable read-only APIs for agents:

- `GET /sessions/{id}/node`
- `GET /sessions/{id}/semantic`
- `GET /sessions/{id}/rendered`

These are examples, not final route commitments.

### Stage B: Structured intervention requests

Introduce explicit runtime actions for agents that are still safe and understandable.

Examples:

- request control
- release control
- send bounded terminal input
- request a snapshot refresh
- tag or annotate session state

### Stage C: Higher-level agent interaction

Only after observation and supervision are stable should we consider a cleaner inter-agent API surface for:

- task handoff
- session notes
- prompt/approval requests
- structured agent coordination metadata

This should not begin as an attempt to fully replace PTY interaction.

## Recommended Development Order

This is the main consolidation outcome: an implementation order that is realistic.

### Phase 1: Harden canonical terminal state

Goal:

- make the terminal state layer authoritative and usable

Deliverables:

- stable canonical terminal state model
- revisioned snapshots/diffs
- bounded scrollback
- main/alternate screen handling
- reusable rendered-text projections

Inputs:

- `thoughts/terminal_model.md`
- `future/session_terminal_multiplexer_v1.md`

### Phase 2: Add interaction classification

Goal:

- classify session interaction mode without semantic interpretation

Deliverables:

- `InteractionSignals`
- `SessionInteractionKind`
- runtime-owned classification updates
- safe inventory-facing exposure

Inputs:

- `thoughts/interaction.md`

### Phase 3: Improve human-readable inventory and node summaries

Goal:

- make session inventory and session cards more legible for humans before deeper semantic extraction work expands

Priority:

- this should come before broader signal-model expansion because human-readable supervision is the first practical product win

Deliverables:

- a minimal `SessionNodeSummary`
- inventory/session card updates built on that summary
- more readable session-state surfaces that are not just lifecycle plus raw timestamps

Suggested minimal shape:

```text
SessionNodeSummary
- sessionId
- lifecycleStatus
- interactionKind
- attentionState
- semanticPreview
- recentFileChangeCount
- gitDirty
- lastActivityAt
```

Notes:

- `semanticPreview` may initially be sparse or low-confidence
- the point is to establish the summary surface now, then improve its internals later

### Phase 4: Unify signal model with runtime summaries

Goal:

- make supervision and attention sit on a cleaner signal base

Deliverables:

- cleaner derived signal layer
- attention inputs that combine:
  - PTY activity
  - file activity
  - process state
  - controller changes
  - interaction kind

Inputs:

- `future/session_signal_map.md`
- `thoughts/signal_map.md`

### Phase 5: Add semantic extraction

Goal:

- generate weak but useful semantic hints from terminal state and signals

Deliverables:

- bottom status line extraction
- prompt/waiting hints
- visible error hints
- semantic preview object

Inputs:

- `future/pty_semantic_extractor.md`

### Phase 6: Expose agent observation APIs

Goal:

- let other agents fetch useful runtime-owned state without scraping raw ANSI

Deliverables:

- stable read-only agent observation endpoints
- machine-usable session node summary
- rendered text and semantic preview fetches

### Phase 7: Add bounded agent interaction APIs

Goal:

- introduce more explicit runtime interaction surfaces for other coding CLIs and agents

Deliverables:

- explicit intervention/action endpoints
- clear ownership rules with the existing controller model
- auditability and recoverability

## What Not To Do

To keep this development real rather than speculative, avoid these traps:

### 1. Do not skip canonical terminal state

If we skip the terminal-state layer and jump directly from PTY bytes to semantic guesses, the result will be noisy and brittle.

### 2. Do not overclaim semantics

The runtime should not pretend it fully understands what the CLI "means".

The extractor should produce hints with confidence boundaries, not fake certainty.

### 3. Do not make the first agent API "just raw PTY but renamed"

That does not solve the product problem.

The value is in giving agents a better runtime-owned view of session state.

### 4. Do not block on full multi-agent orchestration

The next real milestone is better observation and monitoring.

The future mesh/orchestration story should be built on top of that, not used as an excuse to postpone foundational work.

## Migration And Compatibility

This work should be introduced additively.

- current APIs remain
- new terminal-state and node-summary surfaces are additive
- semantic extraction stays out of the critical control path initially

## Debug Logging And Rollout Guidance

This work should be observable while it is still immature.

- add debug traces behind existing runtime trace gates rather than unconditional logs
- prefer short structured fields over dumping raw terminal payloads
- log classifier and summary transitions only when values change
- keep semantic-extraction logging out of hot control paths by default
- make it easy to diff:
  - raw runtime status
  - derived interaction kind
  - attention state
  - semantic preview

Early debug logging should focus on:

- interaction-kind transitions
- node-summary field changes
- semantic-preview updates
- any disagreement between raw session state and derived summary state

Rollout rule:

- if a derived field is uncertain, emit `unknown` or an empty preview rather than an overconfident guess

## Proposed Immediate Deliverables

For real next-step development, the most useful concrete outputs would be:

1. a first-class `SessionInteractionKind` and `InteractionSignals` implementation
2. a more explicit canonical terminal-state interface around the current multiplexer
3. a runtime-owned `SessionNodeSummary` or equivalent inventory-facing object
4. inventory/session card updates built on that summary
5. a first semantic preview payload derived from terminal state and current signals
6. additive read-only APIs for agent observation

That sequence is the most practical path from "persistent remote terminal runtime" to "across-device agent session network with usable supervision and interaction surfaces."

## Relationship To Existing Drafts

This document consolidates and narrows:

- `thoughts/terminal_model.md`
- `thoughts/interaction.md`
- `thoughts/signal_map.md`
- `thoughts/scratches.md`
- `future/pty_semantic_extractor.md`
- `future/session_signal_map.md`
- `future/session_terminal_multiplexer_and_semantic_runtime.md`

Those documents remain useful as deeper references, but this file should be the main starting point for turning the idea into real implementation work.
