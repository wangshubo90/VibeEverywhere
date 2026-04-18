# Environment Management

This note describes how session environment resolution works in the current Sentrits branch.

## Model

Environment handling is split into two layers:

- per-session intent in `include/vibe/session/env_config.h`
- daemon-wide policy in `include/vibe/store/host_config_store.h`

Per session, we decide:

- `mode`
- `overrides`
- `env_file_path`

Daemon-wide, we currently persist:

- `bootstrap_shell_path`
- `import_service_manager_environment`
- `service_manager_environment_allowlist`

That host-level policy lives in `HostIdentity` because it affects how the daemon resolves env for any session it spawns.

## Create Flow

When a session is created in `src/service/session_manager.cpp`, the daemon picks the env mode like this:

- explicit `env_mode` from the request wins
- `command_shell` defaults to `LoginShell`
- everything else defaults to `BootstrapFromShell`

That means provider launches and direct `commandArgv` launches both default to bootstrap env, not login-shell mode.

Then `SessionManager` builds an `EnvConfig`, loads `HostIdentity`, and calls `ResolveEnvironment(...)` in `src/session/env_resolver.cpp`.

## The Three Modes

### `Clean`

- starts from a minimal base
- currently sets `PATH=/usr/bin:/bin:/usr/sbin:/sbin`
- also carries through basic values like `HOME`, `USER`, `TMPDIR`
- then layers `.env` / `.env.in`
- then provider overrides
- then session overrides

### `LoginShell`

- means the spawned process is itself a login-shell-style session
- we do not pre-resolve a full env map
- we only store requested overrides, and apply them at exec time
- this is the most shell-behavior-oriented path

### `BootstrapFromShell`

- daemon runs a login shell once, captures `env -0`, caches it, and uses that result as the base env
- then optionally overlays service-manager allowlisted vars
- then `.env`
- then provider overrides
- then session overrides

This mode is meant to make daemon-created sessions behave more like a user shell without launching every process through `bash -il -c ...`.

## Bootstrap Details

The bootstrap capture is in `src/session/bootstrapped_env_cache.cpp`.

What it does:

- runs `<shell> -l -c 'command -p env -0'`
- captures `stdout` as env data
- captures `stderr` separately
- strips noise keys like `SHLVL`, `_`, `PWD`, `OLDPWD`
- caches by shell path with a 300-second TTL

Failure policy:

- non-zero exit or timeout is a hard error
- successful bootstrap with stderr is allowed, but logged as a daemon warning
- that warning is also stored on the `EffectiveEnvironment` as `bootstrap_warning`

If shell init prints warnings during bootstrap, session creation can still succeed, but the daemon logs the warning.

## Layering And Precedence

The effective order is:

1. base mode result
2. `.env.in` / `.env` or explicit env file
3. provider config overrides
4. session overrides

Session overrides always win.

The `.env` parsing logic is in `src/session/env_file_parser.cpp`. It supports:

- `export KEY=...`
- quoted and unquoted values
- `$VAR` and `${VAR}` expansion against the env accumulated so far

Default file lookup is:

- `workspace/.env.in`
- then `workspace/.env`

If `env_file_path` is explicit, relative paths are resolved from `workspace_root`.

## Execution Path

Once resolved, the env is attached to the `LaunchSpec` in `src/session/launch_spec.cpp`, then consumed in `src/session/posix_pty_process.cpp`.

Execution behavior differs by mode:

- `LoginShell`
  - applies overrides via `setenv()`
  - uses inherited daemon environment
  - launches with `execvp()`
- `Clean` / `BootstrapFromShell`
  - builds an explicit `envp`
  - resolves the executable via `PATH` from that env
  - launches with `execve()`

That split is why daemon env issues matter so much: for explicit-env modes, we are not relying on the daemon's live inherited process env except where we intentionally copy from it.

## What Users Can See

The daemon stores the resolved env on the session entry and exposes it via `GET /sessions/{id}/env` in `src/net/http_shared.cpp`, serialized in `src/net/json.cpp`.

That includes:

- ordered entries
- each variable's source
- `mode`
- `bootstrapShellPath`
- `envFilePath`
- `bootstrapWarning`

The CLI also sends env settings on create and can fetch session env through `src/cli/daemon_client.cpp`.

## Current Gaps

Two practical limitations remain:

- host-level env policy is persisted, but not fully wired into the host-config API yet
- `LoginShell` is intentionally different from `BootstrapFromShell`; it is shell-driven at launch time, not pre-resolved into a full explicit env map

Short version:

- `Clean` = deterministic minimal env
- `LoginShell` = let the launched shell define reality
- `BootstrapFromShell` = capture shell reality once, then launch deterministically from that captured env
