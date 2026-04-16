# Command, Session Setup, And Host Identity Alignment Prompt

Work across the Sentrits runtime and maintained clients:

- Runtime repo: `https://github.com/shubow-sentrits/Sentrits-Core` or `./Sentrits-Core`
- Web client repo: `https://github.com/shubow-sentrits/Sentrits-Web` or `../Sentrits-Web`
- iOS client repo: `https://github.com/shubow-sentrits/Sentrits-IOS` or `../Sentrits-IOS`

Goal:

Align runtime, web, and iOS around session creation inputs, one host-owned reusable launch-record store, and host identity management.

This is a real product-surface alignment task. Use code as truth and produce a concrete implementation plan, not a broad redesign memo.

## Problem Areas

### 1. Session command / reusable launch record / provider hint

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
- reusable launch records should be host-owned
- users should be able to create a new session from a previously used launch record
- Sentrits should support command expansion patterns such as `codex "$(cat prompt.md)"` when creating a new session from CLI and client surfaces

Think in terms of a host-owned reusable launch record that can cover:

- workspace root
- launch command
- optional provider hint or compatibility classification if still useful
- title
- optional conversation id
- optional tags or lightweight metadata if already supported or clearly justified

Do not assume the current UX is correct. Check what already exists in code first.

Important framing:

- Sentrits should not assume sessions are primarily about a named provider.
- It should be able to launch coding CLIs, shell sessions, and ordinary commands like `htop`, `ls`, or `/bin/bash`.
- Provider may remain in the system, but command + workspace is the central setup truth.
- Title, ids, and similar fields are session metadata rather than the core of the reusable launch model.

Important product realignment:

- do not design separate first-class host models for:
  - explicit saved setups
  - recent history
- instead, converge on one host-owned bounded launch-record store
- the host should keep a configurable recent record store with max `N`
- clients should query that store and let users quickly relaunch from it
- if a curated layer is still needed, prefer client-side pin/favorite behavior first rather than a second host-owned preset model

That means this task should explicitly evaluate whether the current host `sessionSetups` model should be collapsed into a more general launch-record store rather than extended further.

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
- what fields belong in the host-owned launch record
- whether current host `sessionSetups` should be migrated/replaced by that launch-record store
- how command/workspace/provider/title/conversation metadata should relate
- how client-side pin/favorite should relate to the host record store

3. Concrete implementation plan
- runtime changes
- API changes
- CLI changes
- web changes
- iOS changes
- migration / compatibility notes

The implementation plan should assume this user flow target for iOS:

- device play card opens a picker-first create flow
- first-class choices are:
  - recent launches from host
  - optionally pinned/favorited entries on the client
  - custom launch
- the current full custom form should not be the default first screen for ordinary session creation

4. Explicit open questions
- only if they remain after code inspection

## Constraints

- Use current code as truth.
- Prefer additive evolution over breaking protocol changes where possible.
- Keep the runtime as the source of truth for host-owned setup/config.
- Do not drift into session-network-v2 planning.

Specific direction:

- do not add a second overlapping host-owned store if one generalized bounded launch-record store can replace the current saved-setup concept cleanly
- be explicit about naming:
  - if `setup` is no longer the right concept, say so
  - if CLI/API compatibility requires keeping the word temporarily, call out the compatibility layer directly

## Execution Instructions

- Create a dedicated git worktree before implementation work starts.
- Validate locally with relevant build and test commands before finishing.
- When finished, create a PR using the repo's PR template.
