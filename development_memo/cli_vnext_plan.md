# CLI vNext Plan

## Goal

The next CLI version should support clean headless administration of a host runtime without requiring the host admin UI.

It should work well for:

- local host development
- remote server deployment over SSH
- operator scripting with `--json`

It should not introduce a second management model that diverges from the runtime API or GUI behavior.

## Current CLI Baseline

Current commands in `sentrits`:

- `serve`
- `local-pty`
- `session-start`
- `list`
- `session-attach`

This is enough for local host workflows, but it is too flat and too narrow for remote server administration.

## Design Principles

- use one command tree with stable nouns
- human-readable output by default
- machine-readable output with `--json`
- keep interactive attach separate from admin/reporting commands
- keep compatibility aliases for current flat commands during migration
- reflect runtime concepts already in the code:
  - one session
  - one active controller
  - many observers
  - supervision state
  - attention state
  - PTY size

## Proposed Top-Level Structure

```text
sentrits serve
sentrits host ...
sentrits session ...
sentrits auth ...
sentrits config ...
sentrits local-pty ...
```

## Session Commands

### `session list`

List sessions with compact status output.

Default columns:

- session id
- title
- status
- supervision
- controller
- attached count

Flags:

- `--json`
- `--all`
- `--group <tag>`
- `--status <status>`
- `--supervision <active|quiet|stopped>`

Compatibility alias:

- `sentrits list`

### `session show <id>`

Show one session in detail.

Default fields:

- identity:
  - session id
  - title
  - provider
  - workspace root
- lifecycle:
  - status
  - supervision state
  - attention state
  - attention reason
- control:
  - controller kind
  - attached count
- terminal:
  - PTY cols
  - PTY rows
- timing:
  - created at
  - last output at
  - last activity at
- git/workspace:
  - branch
  - dirty summary
  - recent file changes

Flags:

- `--json`

### `session start`

Create a session without implicitly attaching unless requested.

Flags:

- `--title <title>`
- `--workspace <path>`
- `--provider <codex|claude>`
- `--group <tag>` repeatable
- `--attach`
- `--json`

Compatibility alias:

- `sentrits session-start`

Recommended behavior:

- default behavior should create and print the session id
- `--attach` should create and immediately attach as host controller

### `session attach <id>`

Host-local interactive attach.

Flags:

- `--observer`
- `--json` should be rejected or ignored for interactive attach

Compatibility alias:

- `sentrits session-attach <id>`

### `session stop <id>`

Direct stop of a live session.

Flags:

- `--json`

### `session clear`

Remove inactive sessions.

Flags:

- `--json`
- `--all-hosts` if the CLI later grows multi-host inventory support

### `session groups add <id> <tag>`

Add a group tag.

### `session groups remove <id> <tag>`

Remove a group tag.

## Host Commands

### `host status`

Show daemon and listener state.

Fields:

- admin host/port
- remote host/port
- discovery on/off
- storage root
- TLS configured or not
- host display name

Flags:

- `--json`

### `host info`

Show the host identity and operator-facing metadata.

### `host pairings list`

List paired devices/tokens.

Fields:

- device label
- token id or short fingerprint
- created at
- last used at if available

Flags:

- `--json`

### `host pairings revoke <id>`

Revoke one device/token.

## Auth Commands

These are lower priority than session and host commands, but they belong in the vNext command tree.

- `auth token create`
- `auth token list`
- `auth token revoke <id>`

If local admin pairing grows a pure CLI path later:

- `auth local-claim`

## Config Commands

These are useful once headless deployment becomes more common.

- `config show`
- `config paths`
- `config set <key> <value>`
- `config validate`

## Output Rules

### Human Output

Default output should be compact and readable in terminals over SSH.

Examples:

```text
$ sentrits session list
12  build-fix      running  active   host    1 attached
15  docs-cleanup   running  quiet    remote  2 attached
18  test-run       exited   stopped  none    0 attached
```

```text
$ sentrits session show 15
Session:        15
Title:          docs-cleanup
Provider:       codex
Workspace:      /srv/repo
Status:         running
Supervision:    quiet
Attention:      none
Controller:     remote
PTY Size:       120 x 48
Branch:         main
Dirty:          3 modified, 0 staged, 1 untracked
```

### JSON Output

All non-interactive data-returning commands should support `--json`.

Initial coverage:

- `session list`
- `session show`
- `session start`
- `session stop`
- `session clear`
- `host status`
- `host info`
- `host pairings list`

## Compatibility Plan

Keep current flat verbs as aliases for one release cycle:

- `list` -> `session list`
- `session-start` -> `session start --attach`
- `session-attach` -> `session attach`

The help output should prefer the new command tree and mark the flat verbs as compatibility aliases.

## Recommended Implementation Order

### Phase 1

- introduce parser/library abstraction
- implement top-level nouns:
  - `host`
  - `session`
- keep old aliases working

### Phase 2

- implement:
  - `session list`
  - `session show`
  - `session start`
  - `session stop`
  - `session clear`

### Phase 3

- implement:
  - `host status`
  - `host info`
  - `host pairings list`
  - `host pairings revoke`

### Phase 4

- add `--json` consistently
- normalize exit codes
- improve help and examples

## Argument Parsing Library Recommendation

Recommended library: `CLI11`

Why it fits this CLI best:

- strong subcommand model
- easy aliases for backward compatibility
- good help generation
- header-only and widely used
- supports validators, defaults, and nested command trees cleanly

Relevant sources:

- CLI11 GitHub: https://github.com/CLIUtils/CLI11
- CLI11 book: https://cliutils.github.io/CLI11/book/

Alternatives considered:

- `cxxopts`
  - good lightweight option parser
  - better fit for flatter CLIs than a deeper noun/subcommand tree
  - source: https://github.com/jarro2783/cxxopts
- `Lyra`
  - clean composable design
  - smaller ecosystem and less obvious fit for a command tree this large
  - source: https://www.bfgroup.xyz/Lyra/

## Recommendation

Adopt `CLI11` and migrate the current hand-rolled `main.cpp` parsing to a structured command tree.

The first milestone should be:

- `session list`
- `session show`
- `session start`
- `session stop`
- `session clear`
- `host status`

That would make the CLI viable for remote headless administration without requiring the host admin UI.
