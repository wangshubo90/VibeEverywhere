# Command, Session Setup, And Host Identity Alignment Prompt

Work across the Sentrits runtime and maintained clients:

- Runtime repo: `https://github.com/shubow-sentrits/Sentrits-Core` or `./Sentrits-Core`
- Web client repo: `https://github.com/shubow-sentrits/Sentrits-Web` or `../Sentrits-Web`
- iOS client repo: `https://github.com/shubow-sentrits/Sentrits-IOS` or `../Sentrits-IOS`

Goal:

Align runtime, web, and iOS around session creation inputs, reusable host-owned session setups, and host identity management.

This is a real product-surface alignment task. Use code as truth and produce a concrete implementation plan, not a broad redesign memo.

## Problem Areas

### 1. Session command / reusable setup / provider hint

Sentrits currently has partial support for:

- providers
- provider command overrides
- explicit session commands
- session title
- workspace root
- persisted host identity / config

But the user-facing setup model is still fragmented across runtime CLI, web, and iOS.

The target direction is:

- command and workspace should be the center of session setup across runtime CLI, web, and iOS
- custom command support should be consistent across runtime CLI, web, and iOS
- reusable session setup should be host-owned
- users should be able to create a new session from a previously used setup
- Sentrits should support command expansion patterns such as `codex "$(cat prompt.md)"` when creating a new session from CLI and client surfaces

Think in terms of a host-owned reusable setup record that can cover:

- workspace root
- launch command
- optional provider hint or compatibility classification if still useful
- title
- optional conversation/history-friendly setup reuse if already supported or feasible

Do not assume the current UX is correct. Check what already exists in code first.

Important framing:

- Sentrits should not assume sessions are primarily about a named provider.
- It should be able to launch coding CLIs, shell sessions, and ordinary commands like `htop`, `ls`, or `/bin/bash`.
- Provider may remain in the system, but command + workspace is the central setup truth.
- Title, ids, and similar fields are session metadata rather than the core of the reusable setup model.

### 2. Host display name / host identity editing

Host name should be treated as a real host-owned setting.

Need to determine:

- where host name is persisted today
- whether it is set only at daemon bootstrap or is already mutable
- how runtime CLI, web, and iOS currently expose it
- what the clean source-of-truth model should be

Target outcome:

- host name can be set before registering/enabling the user daemon if needed
- or changed later through all three surfaces if that is the better model

Do not hand-wave this. Check actual persistence and API seams first.

## Existing Runtime Anchors To Inspect

At minimum inspect:

- `src/store/file_stores.cpp`
- `src/session/provider_config.cpp`
- `src/session/launch_spec.cpp`
- `src/main.cpp`
- `src/net/http_shared.cpp`
- `tests/net/request_parsing_test.cpp`
- `tests/net/http_server_integration_test.cpp`
- `development_memo/implementation_plan.md`

Also inspect current session-create and host-config flows in:

- `https://github.com/shubow-sentrits/Sentrits-Web`
- `https://github.com/shubow-sentrits/Sentrits-IOS`

## Deliverables

Produce:

1. Current-state audit
- what exists today in runtime
- what exists today in web
- what exists today in iOS
- where they differ

2. Proposed source-of-truth model
- what belongs in host config
- what belongs in session create request
- what should be reusable as prior setup / preset
- how command/workspace/provider/title/conversation metadata should relate

3. Concrete implementation plan
- runtime changes
- API changes
- CLI changes
- web changes
- iOS changes
- migration / compatibility notes

4. Explicit open questions
- only if they remain after code inspection

## Constraints

- Use current code as truth.
- Prefer additive evolution over breaking protocol changes where possible.
- Keep the runtime as the source of truth for host-owned setup/config.
- Do not drift into session-network-v2 planning.

## Execution Instructions

- Create a dedicated git worktree before implementation work starts.
- Validate locally with relevant build and test commands before finishing.
- When finished, create a PR using the repo's PR template.
