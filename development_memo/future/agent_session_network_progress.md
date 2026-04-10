# Agent Session Network Progress

Status: in progress

- [x] Consolidate future design notes into a development-facing plan
- [x] Add current-reality vs next-phase target and migration notes
- [x] Add initial runtime-owned interaction classification seam
- [x] Add minimal runtime-owned `SessionNodeSummary` surface
- [x] Expose additive node-summary and interaction fields through session list JSON
- [x] Expose additive node-summary and interaction fields through snapshot JSON
- [x] Keep initial semantic preview intentionally sparse and out of control path
- [x] Add focused tests for new additive summary parsing / emission
- [x] Review CLI human output for useful low-risk summary improvements
- [x] Document follow-up implementation gaps after the first milestone lands

## Follow-up Gaps After Milestone 1

- interaction classification is still conservative and does not identify fullscreen/TUI sessions yet
- semantic preview is attention-driven and intentionally sparse; it is not a semantic extractor yet
- node summary exists on current summary/snapshot surfaces, but no dedicated runtime observation endpoint exists yet
- CLI human output now shows interaction and preview, but it is still tab-oriented rather than a richer inventory presentation
- derived-field transition tracing now exists, but it still needs real coding-CLI smoke data before classifier tuning

## Live Smoke Trace Setup

Use these env vars when smoking a real coding CLI session:

- `SENTRITS_DEBUG_TRACE=1`
  - emits gated stderr traces
- `SENTRITS_SESSION_SIGNAL_TRACE_PATH=/tmp/sentrits-session-signal-trace.log`
  - writes derived session-node transition records to a file

Current derived trace scope:

- scope: `core.node`
- event: `summary.transition`

What the trace currently records:

- session id
- lifecycle status
- controller kind
- interaction kind
- attention state
- semantic preview
- recent file-change count
- git dirty state
- current sequence
- last activity time when available
- transition reason such as:
  - `create`
  - `poll`
  - `request_control`
  - `release_control`
  - `file_change`
  - `git_change`
  - `stop`

## Current Open Investigation Notes

- iOS focused-view jitter under typing plus live output:
  - strongest current suspect is a SwiftTerm viewport-anchor race
  - the wrapper currently preserves/restores viewport around `feed`, `showCursor`, and `scrolled`
  - this likely fights SwiftTerm's own caret-follow scroll behavior and causes visible up/down vibration

- Codex long-session history collapse in remote control:
  - strongest current suspect is alternate-screen/fullscreen redraw behavior rather than simple byte loss
  - `TerminalMultiplexer` enables alt-screen support, but Sentrits currently does not record or expose alt-screen state
  - if Codex enters a transient alternate/fullscreen state during `working` / `thinking`, observer/bootstrap surfaces can capture that temporary visible model instead of the stable conversation history
