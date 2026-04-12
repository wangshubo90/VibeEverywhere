# Future Docs Realignment Prompt: Semantic Monitor vs Stale Multiplexer Notes

Work in `Sentrits-Core`.

Goal:

Review `development_memo/future/` and realign it to the actual current codebase and current roadmap.

Important scope rule:

- session network v2.0 is later work; do not optimize this pass around it
- focus on semantic monitor / semantic preview / monitoring surfaces
- identify which multiplexer notes are still useful and which are stale

## Known Context

Some of the semantic-monitor foundation is already implemented.

Current evidence already in repo history and docs includes:

- initial runtime-owned interaction classification
- additive `SessionNodeSummary`
- additive `semanticPreview`
- conservative session-node transition tracing

Relevant anchors:

- `development_memo/future/agent_session_network_progress.md`
- `development_memo/future/agent_session_network_development_plan.md`
- `development_memo/future/pty_semantic_extractor.md`
- `development_memo/future/session_signal_map.md`
- `development_memo/future/session_terminal_multiplexer_and_semantic_runtime.md`
- `development_memo/future/session_terminal_multiplexer_v1.md`
- `development_memo/future/session_terminal_multiplexer_wire_compatibility.md`

Also inspect code and tests around:

- `src/net/http_shared.cpp`
- `src/session/session_runtime.cpp`
- `src/session/terminal_multiplexer.cpp`
- `tests/session/session_snapshot_test.cpp`
- `tests/cli/daemon_client_test.cpp`

And inspect git history for semantic/monitor/multiplexer milestones.

## Core Questions

1. What semantic-monitor / observation work is already done?
- runtime surfaces
- JSON fields
- CLI support
- traces
- tests

2. What meaningful semantic-monitor work is still left for the next real phase?
- especially things that improve human supervision and low-risk agent observation

3. Which multiplexer documents are still actionable?
- keep if they still describe unfinished real implementation work
- mark stale or remove if they are superseded by current implementation or newer docs

4. What should the next future-doc set emphasize?
- better observation
- more legible inventory/session summary
- semantic preview improvements
- additive agent-facing observation surfaces

## Deliverables

Produce:

1. Current-state audit
- what is already implemented
- where it is exposed
- what is still intentionally sparse / conservative

2. Future-doc cleanup plan
- docs to keep
- docs to rewrite
- docs to archive/remove as stale

3. Next-phase plan
- prioritized leftovers for semantic monitor / supervision
- explicit deprioritization of session-network-v2 work for this pass

4. Suggested doc outputs
- updated progress doc
- updated future backlog doc
- removal/rewrite list for stale multiplexer notes

## Constraints

- Code and merged history win over speculative future docs.
- Do not expand the roadmap; tighten it.
- Keep semantic extraction out of the critical control path unless current code already does otherwise.
- Be explicit about what is already implemented versus what is still only design text.

## Execution Instructions

- Create a dedicated git worktree (something like ~/dev/Sentrits-Repo-worktree) before implementation work starts.
- Validate locally with relevant build and test commands before finishing.
- When finished, create a PR using the repo's PR template.
