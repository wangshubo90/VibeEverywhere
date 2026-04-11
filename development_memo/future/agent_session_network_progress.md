# Agent Session Network Progress

Status: semantic-monitor foundation landed; next phase should stay additive and observation-focused.

This document is the current-state audit for the future-doc set under `development_memo/future/`.

Code and merged history win over older target-state notes. The main reference points for the current implementation are:

- `src/service/session_manager.cpp`
- `src/session/session_runtime.cpp`
- `src/session/terminal_multiplexer.cpp`
- `src/net/json.cpp`
- `src/net/http_shared.cpp`
- `src/main.cpp`
- `tests/service/session_manager_test.cpp`
- `tests/net/http_json_test.cpp`
- `tests/session/session_snapshot_test.cpp`
- `tests/cli/daemon_client_test.cpp`
- commits `36aa2cd`, `ea2dd76`, `f2e11ae`

## What Is Already Implemented

### Runtime-owned monitoring surfaces

- `SessionManager` now derives additive supervision fields from runtime, file, git, and controller state.
- `SessionSignals` exists on snapshots and carries:
  - supervision state
  - attention state and reason
  - interaction kind
  - recent file-change count
  - git branch and dirty counters
  - recent timing fields
  - PTY size and sequence watermark
- `SessionNodeSummary` exists on both session summaries and snapshots and carries:
  - lifecycle status
  - interaction kind
  - attention state
  - semantic preview
  - recent file-change count
  - git dirty flag
  - last activity time when known
- `semanticPreview` exists as an intentionally sparse derived string:
  - `Awaiting input`
  - `Session error`
  - `Workspace changed`
  - `Git state changed`
  - `Controller changed`
  - `Session exited cleanly`
  - `Workspace dirty`

### JSON and API exposure

- `GET /sessions` exposes additive monitoring fields on each session summary:
  - `supervisionState`
  - `attentionState`
  - `attentionReason`
  - `interactionKind`
  - `semanticPreview`
  - `gitDirty`
  - `gitBranch`
  - git counters
  - `nodeSummary`
- `GET /sessions/{id}` returns the same summary surface.
- `GET /sessions/{id}/snapshot` exposes:
  - top-level `interactionKind`
  - top-level `semanticPreview`
  - `signals`
  - `nodeSummary`
  - additive `terminalScreen`
  - additive `terminalViewport` when `viewId`, `cols`, and `rows` are supplied

### CLI support

- `src/cli/daemon_client.cpp` parses additive `interactionKind` and `semanticPreview` from session lists.
- `src/main.cpp` shows those fields in:
  - human `session list`
  - human `session snapshot`

The CLI support is useful, but still shallow. It is tabular and summary-oriented rather than a richer monitoring surface.

### Terminal-state foundation already in code

- `SessionRuntime` owns a `TerminalMultiplexer`.
- the runtime records a canonical `terminal_screen` snapshot on startup, resize, and PTY output
- `TerminalMultiplexer` already provides:
  - canonical visible screen
  - bounded scrollback
  - render revision
  - bootstrap ANSI for rehydration
  - per-view viewport snapshots with horizontal clipping and cursor-follow behavior

This matters because the next semantic-monitor work can build on canonical terminal state instead of scraping raw PTY tail text alone.

### Trace and investigation support

- `SessionManager` emits conservative session-node transition traces under:
  - scope `core.node`
  - event `summary.transition`
- `SENTRITS_SESSION_SIGNAL_TRACE_PATH` writes those derived transitions to a file.
- `TerminalMultiplexer` also emits targeted terminal debug traces for clear-screen and alternate-screen style escape activity.

### Test coverage already present

- `tests/service/session_manager_test.cpp`
  - attention and file-change inference
- `tests/net/http_json_test.cpp`
  - summary, snapshot, and event JSON with additive monitoring fields
- `tests/session/session_snapshot_test.cpp`
  - snapshot structs for monitoring fields
- `tests/cli/daemon_client_test.cpp`
  - additive CLI parsing
- `tests/session/terminal_multiplexer_test.cpp`
  - canonical screen, scrollback, bootstrap ANSI, and viewport behavior

## Semantic Monitoring Available Today

This is the practical semantic-monitor inventory available right now.

### Runtime

- supervision state:
  - `active`
  - `quiet`
  - `stopped`
- attention state and reason:
  - `none`
  - `info`
  - `action_required`
  - `intervention`
- interaction kind:
  - `unknown`
  - `running_non_interactive`
  - `interactive_line_mode`
  - `completed_quickly`
- additive node summary:
  - lifecycle
  - interaction
  - attention
  - semantic preview
  - recent file-change count
  - git dirty
  - last activity time
- semantic preview strings:
  - `Awaiting input`
  - `Session error`
  - `Workspace changed`
  - `Git state changed`
  - `Controller changed`
  - `Session exited cleanly`
  - `Workspace dirty`

### Observation surfaces

- session list summaries
- session detail summaries
- session snapshots
- websocket summary and activity/update events
- canonical `terminalScreen`
- additive `terminalViewport`

### Terminal-state foundation

- visible rendered lines
- bounded scrollback
- cursor row and column
- render revision
- bootstrap ANSI snapshot
- viewport projection for a requested observer size

### CLI

- human session list prints `interactionKind` and `semanticPreview`
- human snapshot prints interaction and summary fields

### Trace and debugging

- `core.node:summary.transition`
- optional trace file via `SENTRITS_SESSION_SIGNAL_TRACE_PATH`
- terminal escape/debug traces from `TerminalMultiplexer`

### Current limits

- no dedicated semantic-monitor endpoint yet
- no fullscreen classification yet
- no prompt / approval / progress / error extraction yet
- semantic preview is still intentionally short and conservative

## What Is Still Intentionally Sparse

- interaction classification is conservative
  - it currently distinguishes mostly `unknown`, `running_non_interactive`, `interactive_line_mode`, and quick completion
  - `interactive_fullscreen` exists in the type, but current inference does not detect it
- `semanticPreview` is sparse on purpose
  - it is a short derived hint, not a semantic extractor
- no dedicated semantic-monitor endpoint exists yet
  - monitoring data is exposed only through summary, detail, snapshot, and websocket summary/event surfaces
- semantic extraction is not in the PTY control path
  - current logic is derived from existing runtime metadata and low-risk signals
- terminal-multiplexer work is foundational, not the active roadmap center
  - the code has canonical screen/bootstrap/viewport support
  - the larger "full multiplexer migration" documents now overstate near-term work

## What The Next Real Phase Should Cover

The next phase should improve human supervision and low-risk agent observation without turning semantic extraction into a hard runtime dependency.

Priority order:

1. Better observation surfaces
- add an additive observation-oriented snapshot or monitor payload instead of forcing clients to infer from mixed summary fields
- keep it read-only and runtime-owned

2. More legible inventory and session summaries
- improve session list and websocket summary payloads for supervision use
- keep previews short, stable, and explainable

3. Semantic preview improvements
- use canonical terminal screen and bottom visible lines to produce better hints
- focus on waiting-for-input, approval, progress, and visible error cues
- keep outputs weakly inferred and additive

4. Additive agent-facing observation surfaces
- expose structured read-only state that is better than raw tail text
- avoid any session-network v2 interaction API work in this pass

## Explicitly Not The Focus Of This Pass

- session network v2.0
- agent-to-agent interaction APIs
- replacing controller semantics with a new protocol
- making semantic extraction a critical control-path dependency
- broad multiplexer redesign work beyond already-landed canonical-screen foundations

## Future-Doc Cleanup Result

Keep and update:

- `future/agent_session_network_progress.md`
- `future/agent_session_network_development_plan.md`
- `future/session_signal_map.md`
- `future/pty_semantic_extractor.md`

Remove as stale or superseded for the near roadmap:

- `future/session_terminal_multiplexer_and_semantic_runtime.md`
- `future/session_terminal_multiplexer_v1.md`
- `future/session_terminal_multiplexer_wire_compatibility.md`

Reason:

- those documents were useful while the canonical terminal snapshot/bootstrap work was still speculative
- the main useful parts are now implemented in code
- their remaining open items are either later work or too broad for the current semantic-monitor phase
