# Agent Session Network Development Plan

Status: narrowed to the next semantic-monitor phase.

This document is the active future backlog for observation and supervision work.

It intentionally does not optimize for session network v2.0. The immediate goal is to make live sessions easier for humans and low-risk agent observers to understand using additive runtime-owned state.

## Scope

This phase is about:

- better observation
- better inventory and session summaries
- better semantic previews
- additive agent-facing observation surfaces

This phase is not about:

- new agent-control protocols
- multi-agent session-network orchestration
- replacing the current controller model
- broad multiplexer migration work

## Current Baseline

Already in code:

- canonical terminal screen snapshots with scrollback and bootstrap ANSI
- viewport snapshots for observer-sized reads
- runtime-owned supervision, attention, interaction, git, and file-change fields
- additive `SessionNodeSummary`
- additive `semanticPreview`
- conservative `core.node` transition tracing
- JSON exposure through summaries, snapshots, and websocket summary/event payloads
- basic CLI display of interaction and preview

That baseline is enough to justify moving from "foundational semantics seam" to "better monitoring surfaces."

## Design Rules For The Next Phase

### 1. Keep PTY execution truth separate from semantic hints

Semantic extraction remains advisory.

Do not let:

- prompt detection
- progress detection
- approval detection
- error-region detection

become the source of lifecycle truth.

### 2. Prefer canonical terminal state over raw tail parsing

If a new hint needs terminal context, derive it from:

- `terminalScreen`
- viewport snapshots
- stable bottom visible lines
- existing runtime timing and controller state

Do not build new product surfaces around fragile ANSI chunk parsing alone.

### 3. Keep semantic work out of the control path

New observation logic should be:

- additive
- bounded
- cheap to compute
- safe to omit without breaking session execution

### 4. Optimize for supervision first

The immediate consumers are:

- human inventory and focused monitoring surfaces
- read-only agent observers

Do not expand into write or orchestration APIs in this pass.

## Prioritized Backlog

### Priority 1: Dedicated observation payload

Add an additive observation-oriented surface that packages the most useful monitoring state together.

Candidate contents:

- node summary
- current supervision, attention, and interaction state
- semantic preview
- recent file-change sample
- git summary
- terminal screen revision metadata
- bounded visible text excerpt or bottom-line excerpt
- optional viewport projection for a requested observer size

Why first:

- current data exists, but it is spread across list/detail/snapshot payloads
- this improves both human supervision and agent observation without changing control semantics

### Priority 2: Better inventory legibility

Improve session summaries for inventory surfaces.

Focus:

- shorter and more useful preview text
- clearer distinction between:
  - waiting for input
  - actively producing output
  - recently changed workspace
  - recently changed git state
  - clean exit versus failure
- preserve additive compatibility

### Priority 3: Conservative semantic preview v2

Use terminal-state cues to upgrade `semanticPreview` beyond attention-only text.

Good next targets:

- likely waiting for typed input
- likely approval / confirmation prompt visible
- likely progress loop or status spinner
- likely visible error block
- stable bottom status line excerpt when safe

Guardrails:

- do not require provider-specific parsers
- do not claim certainty where the runtime only has hints
- keep previews short and explainable

### Priority 4: Additive agent-facing read-only observation

Expose runtime-owned observation state that is more useful than raw terminal tail text, but still conservative.

Good candidates:

- structured node summary
- bounded rendered text window
- recent semantic hints
- recent file and git state
- render revision and activity timing

This should stay read-only in this phase.

## Low-Hanging Fruit For Clients

These are additive client features that can ship against the current runtime surfaces or very small runtime extensions.

### Using already-available fields

- better session-list badges for:
  - `interactionKind`
  - `attentionState`
  - git dirty state
  - recent file-change count
- richer preview rows using:
  - `semanticPreview`
  - `recentFileChangeCount`
  - `lastActivityAtUnixMs`
- focused-view observation summary card using:
  - `nodeSummary`
  - `signals`
  - git summary
  - current sequence / render revision
- clearer stale-vs-active indicators using:
  - `supervisionState`
  - `lastOutputAtUnixMs`
  - `lastActivityAtUnixMs`
- explicit awaiting-input and recent-workspace-change callouts where current attention reasons already support them

### With only small additive runtime help

- show a bounded bottom-line preview sourced from `terminalScreen.visibleLines`
- show a compact rendered-text excerpt in inventory or focused monitoring panels
- show render-revision changes as a lightweight activity pulse without raw stream inspection

These are good MVP client wins because they improve supervision without requiring new control APIs or session-network-v2 work.

## Deprioritized For Now

These items are real future work, but not part of the next doc set or implementation pass:

- session network v2.0
- agent-to-agent interaction APIs
- richer controller/stdin-owner protocol migration
- generalized graph or network topology APIs
- large multiplexer compatibility migration plans

## Expected Doc Outputs

The tightened future-doc set should stay small:

1. `agent_session_network_progress.md`
- current-state audit
- implemented surfaces
- explicit sparse/conservative areas
- doc cleanup result

2. `agent_session_network_development_plan.md`
- active next-phase backlog
- prioritized semantic-monitor work
- explicit v2 deprioritization

3. `session_signal_map.md`
- current implemented signals first
- only a small next-step expansion list

4. `pty_semantic_extractor.md`
- narrow extractor backlog for low-risk terminal-state hints

## Exit Criteria For This Phase

This phase is successful when Sentrits can do all of the following without changing the control model:

- show a more legible supervision summary for live sessions
- expose a dedicated observation-oriented payload
- provide a better semantic preview than simple attention-reason text
- remain conservative about fullscreen and deeper provider-specific inference
