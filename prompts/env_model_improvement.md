# Agent Prompt: Environment Model Improvement

## Context

Sentrits runs as a user daemon (macOS launchd / Linux systemd --user). Child
processes spawned via `forkpty()` inherit the daemon's environment, which does NOT
include the user's interactive shell environment (`.zshrc`, `.bashrc`, `nvm`, `brew`,
`pyenv`, etc). This causes PATH mismatches and missing env vars for both human
interactive sessions and agent-driven jobs.

The low-level plumbing already exists: `LaunchSpec::environment_overrides`
(map<string,string>) is passed through `setenv()` before `execvp()` in
`PosixPtyProcess::Start()`. The gap is everywhere above that:

- `CreateSessionRequest` (service layer) — no env fields
- `CreateSessionRequestPayload` (net layer) — no env fields  
- REST POST /sessions — no env fields parsed or serialized
- CLI `sentrits session start` — no `--env` flags

## Goal

Implement an `EnvMode` abstraction that makes session environment:
- deterministic and debuggable
- appropriate for both human interactive and agent-driven sessions
- extensible without breaking existing sessions

---

## New Types to Define

### `include/vibe/session/env_config.h` (new file)

```cpp
namespace vibe::session {

enum class EnvMode {
  Clean,               // Only base vars + explicit overrides + .env file
  LoginShell,          // Session IS a login shell (zsh -l or bash -l)
  BootstrapFromShell,  // Capture env from login shell once, apply to child
};

struct EnvConfig {
  EnvMode mode = EnvMode::BootstrapFromShell;

  // Per-session overrides. Applied on top of whatever the mode produces.
  // Highest precedence — always wins.
  std::unordered_map<std::string, std::string> overrides;

  // Path to .env file. Relative paths resolved from workspace_root.
  // Optional — if absent, defaults to workspace_root/.env if it exists.
  std::optional<std::string> env_file_path;
};

// Source tag for each variable — used for debug/inspect output.
enum class EnvSource {
  DaemonInherited,   // Came from daemon's own environment (base layer)
  ServiceManager,    // Imported via launchd/systemd user-service config
  BootstrapShell,    // Captured from login shell bootstrap
  EnvFile,           // Loaded from .env or .env.in file
  ProviderConfig,    // Set by ProviderConfig::environment_overrides
  SessionOverride,   // Explicitly set by session request or CLI -e flag
};

struct EnvEntry {
  std::string key;
  std::string value;
  EnvSource source;
};

// The resolved, ordered list of env vars for a session.
// Returned by GET /sessions/{id}/env for debuggability.
struct EffectiveEnvironment {
  std::vector<EnvEntry> entries;
  EnvMode mode;
  std::optional<std::string> bootstrap_shell_path;
  std::optional<std::string> env_file_path;
  // Non-fatal diagnostic from bootstrap stderr (truncated). Present when
  // bootstrap ran but emitted warnings (e.g. shell init file errors).
  std::optional<std::string> bootstrap_warning;
};

}  // namespace vibe::session
```

---

## Mode Behavior

### `Clean`

Environment = explicitly constructed, no shell involved.

Layers (later layers override earlier ones):
1. Minimal base: `PATH=/usr/bin:/bin:/usr/sbin:/sbin`, `HOME`, `USER`, `TMPDIR`
2. `.env` file in workspace (if present), parsed as `KEY=VALUE` per line
3. `ProviderConfig::environment_overrides`
4. `EnvConfig::overrides` (session-level)

Use when: CI agent jobs, reproducible builds, any context requiring determinism.

### `LoginShell`

The session process IS the login shell. No environment pre-capture.

- `LaunchSpec::executable` = configured shell (e.g. `/bin/zsh`)
- `LaunchSpec::arguments` = `["-l"]` (login shell flag)
- `EnvConfig::overrides` applied via `setenv()` before `execvp()` as usual

Use when: human interactive sessions where the user expects a real terminal.

### `BootstrapFromShell`

Spawn a non-interactive login shell once, capture its environment via `env -0`,
cache it, then apply to all subsequent sessions using this mode.

Bootstrap command: `<shell> -l -c 'command -p env -0'`

- `-l` = login shell (loads `/etc/profile`, `.zprofile`, `.bash_profile`)  
- `-c 'command -p env -0'` = non-interactive, prints NUL-delimited
  `KEY=VALUE\0` pairs using the system `env`
- NUL delimiter is unambiguous (values may contain newlines, spaces, `=`)
- No prompts, no interactive config noise (`.zshrc` interactive guards are bypassed)

Parsing: split on `\0`, then split each token on first `=` only.

Post-capture filtering — strip shell-internal noise:
- `SHLVL` (shell nesting depth — meaningless for child)
- `_` (last command — shell artifact)
- `PWD` (will be set by `chdir()` later)
- `OLDPWD`

Layers applied on top of bootstrap result:
1. Bootstrap-captured env (base)
2. Service manager imported env (enabled by HostConfig, explicit allowlist only)
3. `.env` file overrides
4. `ProviderConfig::environment_overrides`
5. `EnvConfig::overrides` (session-level)

Use when: agent sessions (Claude, Codex) and human provider sessions that need
real-world PATH but not interactive shell behavior.

---

## BootstrappedEnvCache (daemon-level singleton)

Owned by `SessionManager` (or `HttpServer`). Keyed on shell path string.

```cpp
// include/vibe/session/bootstrapped_env_cache.h

class BootstrappedEnvCache {
 public:
  // Returns cached env or runs bootstrap. Blocking (called before fork).
  // TTL: 300s by default; invalidated on SIGHUP.
  auto Get(const std::string& shell_path)
      -> std::expected<std::unordered_map<std::string, std::string>, std::string>;

  void Invalidate();  // Call on SIGHUP

 private:
  struct Entry {
    std::unordered_map<std::string, std::string> env;
    std::chrono::steady_clock::time_point captured_at;
  };
  std::unordered_map<std::string, Entry> cache_;
  std::chrono::seconds ttl_{300};
};
```

Bootstrap execution constraints:
- Hard timeout: 5 seconds. If the shell hangs, return an error (session fails to
  start with a clear message, not a hang).
- Use `popen()`-style or `pipe() + fork() + execvp()` — NOT `forkpty()` (no TTY
  needed, and a TTY would trigger interactive mode in some shell configs).
- On failure: log the error and fail session creation with a clear diagnostic by
  default. Do not silently fall back to `Clean` unless that fallback behavior is
  explicitly configured.

Also capture bootstrap stderr separately and include a trimmed diagnostic in the
failure path. That matters when shell init files print syntax errors or reference
missing binaries.

Shell path and service-manager import policy: read from `HostConfig`
(configurable), default to:
1. `$SHELL` from daemon's own environment
2. `/bin/zsh` on macOS
3. `/bin/bash` on Linux

HostConfig should also define daemon-wide environment policy:
- `bootstrap_shell_path` (optional override)
- `import_service_manager_environment` (bool)
- `service_manager_environment_allowlist` (list of keys/patterns)

Service-manager env import guidance:
- Linux `systemd --user`: prefer explicit `Environment=` / `EnvironmentFile=`
  in the unit, not opportunistic `import-environment`
- macOS `launchd`: prefer explicit `EnvironmentVariables` in the plist
- Only import a conservative allowlist by default: `PATH`, `HOME`, `USER`,
  `LANG`, `LC_*`, `TMPDIR`, `SHELL`, `NVM_DIR`, `PNPM_HOME`, `BUN_INSTALL`,
  `HOMEBREW_PREFIX`
- Do not automatically import secrets from the service manager into every session

---

## Changes Required — by File

### New files
- `include/vibe/session/env_config.h` — types above
- `include/vibe/session/bootstrapped_env_cache.h` + `.cpp`
- `src/session/env_resolver.h` + `.cpp` — `ResolveEnvironment(EnvConfig, workspace_root, cache) -> EffectiveEnvironment`

### `include/vibe/session/launch_spec.h`
Add field:
```cpp
EnvConfig env_config;  // replaces bare environment_overrides
```
Keep `environment_overrides` or absorb into `EnvConfig::overrides` — if absorbed,
update all call sites.

### `src/session/posix_pty_process.cpp`
Replace `ApplyEnvironmentOverrides()` with `ApplyEffectiveEnvironment()`.
Use `execvpe()` instead of `execvp()` if passing a full env array is cleaner than
`setenv()` (avoids polluting daemon's own env with `setenv()` calls in child):

```cpp
// Build envp array from EffectiveEnvironment
std::vector<std::string> env_strings;
std::vector<char*> envp;
for (const auto& entry : effective_env.entries) {
  env_strings.push_back(entry.key + "=" + entry.value);
  envp.push_back(env_strings.back().data());
}
envp.push_back(nullptr);
execvpe(executable, argv.data(), envp.data());
```

Note: switching to `execvpe()` is safer — `setenv()` in child is fine but mutates
the inherited env table in place, which is messier to reason about.

### `include/vibe/session/launch_spec.cpp` (`BuildLaunchSpec`)
Accept `EnvConfig` as parameter (from `CreateSessionRequest`) and include in
returned `LaunchSpec`.

### `include/vibe/store/host_config_store.h` / host config schema
Add daemon-wide environment policy:
```cpp
std::optional<std::string> bootstrap_shell_path;
bool import_service_manager_environment = false;
std::vector<std::string> service_manager_environment_allowlist;
```

These are host-level settings, not per-session request fields.

### `include/vibe/service/session_manager.h` (`CreateSessionRequest`)
Add:
```cpp
std::optional<vibe::session::EnvMode> env_mode;
std::unordered_map<std::string, std::string> environment_overrides;
std::optional<std::string> env_file_path;
```

Default `env_mode` when absent:
- If `command_shell` or `command_argv` is set (human interactive): `LoginShell`
- If provider session: `BootstrapFromShell`

### `include/vibe/net/request_parsing.h` (`CreateSessionRequestPayload`)
Add:
```cpp
std::optional<std::string> env_mode;  // "clean" | "login_shell" | "bootstrap_from_shell"
std::optional<std::unordered_map<std::string, std::string>> environment_overrides;
std::optional<std::string> env_file_path;
```

### `src/net/request_parsing.cpp` (`ParseCreateSessionRequest`)
Parse new JSON fields:
- `"envMode"` → `env_mode`
- `"environmentOverrides"` → `environment_overrides` (JSON object → map)
- `"envFilePath"` → `env_file_path`

### `src/net/http_shared.cpp` (resolve payload → service request)
Map new fields through.

### `include/vibe/cli/daemon_client.h` (`CreateSessionRequest`)
Add:
```cpp
std::optional<vibe::session::EnvMode> env_mode;
std::unordered_map<std::string, std::string> environment_overrides;
std::optional<std::string> env_file_path;
```

### `src/cli/daemon_client.cpp` (`BuildCreateSessionRequestBody`)
Serialize new fields:
```cpp
if (request.env_mode.has_value()) {
  object["envMode"] = std::string(vibe::session::ToString(*request.env_mode));
}
if (!request.environment_overrides.empty()) {
  json::object env_obj;
  for (const auto& [k, v] : request.environment_overrides) env_obj[k] = v;
  object["environmentOverrides"] = std::move(env_obj);
}
if (request.env_file_path.has_value()) {
  object["envFilePath"] = *request.env_file_path;
}
```

### `src/main.cpp` (CLI `session start` command)
Add flags:
```
--env-mode <mode>     clean | login-shell | bootstrap (default: bootstrap)
-e KEY=VALUE          per-session env override (repeatable)
--env-file <path>     .env file path (default: workspace/.env if present)
```

Parse `-e` flags into a map (split on first `=`).

---

## New REST Endpoint: Inspect Effective Environment

```
GET /sessions/{id}/env
Authorization: same as GET /sessions/{id}

Response 200:
{
  "mode": "bootstrap_from_shell",
  "bootstrapShellPath": "/bin/zsh",
  "envFilePath": "/home/user/project/.env",
  "entries": [
    { "key": "PATH", "value": "/usr/local/bin:/usr/bin:/bin", "source": "bootstrap_shell", "redacted": false },
    { "key": "NVM_DIR", "value": "/home/user/.nvm", "source": "bootstrap_shell", "redacted": false },
    { "key": "DATABASE_URL", "value": "<redacted>", "source": "env_file", "redacted": true },
    { "key": "ANTHROPIC_API_KEY", "value": "<redacted>", "source": "session_override", "redacted": true }
  ]
}
```

Source values: `"daemon_inherited"`, `"bootstrap_shell"`, `"env_file"`,
`"provider_config"`, `"session_override"`, `"service_manager"`.

Redaction policy:
- Default CLI/UI output should redact values for keys matching common secret
  patterns: `*_KEY`, `*_TOKEN`, `*_SECRET`, `*_PASSWORD`, `DATABASE_URL`,
  `AUTH_*`, `AWS_*`
- Return full values only for explicit privileged/local inspection, and even
  then consider `--show-secrets` opt-in on the CLI
- Web UI should never display unredacted secret values by default

Handler: `HandleGetSessionEnvRequest()` in `src/net/http_shared.cpp`.
Requires `SessionManager` to store `EffectiveEnvironment` per session alongside
`SessionEntry` (or re-derive it on demand from stored `LaunchSpec`).

---

## New CLI Subcommand: `sentrits session env {id}`

Calls `GET /sessions/{id}/env` and pretty-prints:

```
Session abc123 — effective environment (mode: bootstrap_from_shell)
Bootstrap shell: /bin/zsh

PATH=/usr/local/bin:/usr/bin:/bin          [bootstrap_shell]
NVM_DIR=/home/user/.nvm                    [bootstrap_shell]
DATABASE_URL=<redacted>                    [env_file: .env]
ANTHROPIC_API_KEY=<redacted>              [session_override]
```

Implementation: new command branch in `src/main.cpp`, new method in
`vibe::cli::DaemonClient`.

---

## .env File Format

Simple line-oriented format. Parsed by new `ParseEnvFile()` utility.

```
# comment
KEY=value
KEY2="value with spaces"
KEY3='single quoted'
MULTI=${EXISTING}:/extra/path   # variable expansion from current env
```

Rules:
- Lines starting with `#` are comments
- Empty lines ignored
- Values optionally quoted (quotes stripped)
- `${VAR}` expanded from the current accumulated env at parse time
- No `export` keyword needed
- `.env.in` = committed template with `${PLACEHOLDER}` vars (no secrets)
- `.env` = local overrides, should be gitignored

`.env.in` and `.env` are both loaded if present; `.env` values win.

---

## Default Behavior (no flags specified)

| Session type | Default EnvMode |
|---|---|
| `--shell-command` (human interactive) | `LoginShell` |
| Provider session (Claude/Codex) | `BootstrapFromShell` |
| `--command-argv` (direct exec) | `BootstrapFromShell` |

Default shell for bootstrap: `$SHELL` from daemon env → `/bin/zsh` (macOS) →
`/bin/bash` (Linux).

If bootstrap fails, the session create response should make that explicit, for
example:

`failed to bootstrap login-shell environment via /bin/zsh: timed out after 5s`

Do not silently fall back to `Clean` unless the caller explicitly requested
fallback behavior.

---

## Checklist for Implementation

- [ ] `include/vibe/session/env_config.h` — new types
- [ ] `include/vibe/session/bootstrapped_env_cache.h` + `.cpp` — cache + bootstrap runner
- [ ] `src/session/env_resolver.cpp` — `ResolveEnvironment()` combining all layers
- [ ] `src/session/env_file_parser.cpp` — `.env` / `.env.in` parser
- [ ] `include/vibe/session/launch_spec.h` — add `env_config` field
- [ ] `src/session/posix_pty_process.cpp` — use `execvpe()` with resolved envp
- [ ] `src/session/launch_spec.cpp` — thread `EnvConfig` through `BuildLaunchSpec`
- [ ] host config model / JSON serialization — add bootstrap shell path and
      service-manager import policy
- [ ] `include/vibe/service/session_manager.h` — `CreateSessionRequest` env fields
- [ ] `src/service/session_manager.cpp` — pass `EnvConfig` to `BuildLaunchSpec`, store `EffectiveEnvironment` in `SessionEntry`
- [ ] `include/vibe/net/request_parsing.h` — `CreateSessionRequestPayload` env fields
- [ ] `src/net/request_parsing.cpp` — parse `envMode`, `environmentOverrides`, `envFilePath`
- [ ] `src/net/http_shared.cpp` — map through + add `GET /sessions/{id}/env` handler
- [ ] `include/vibe/cli/daemon_client.h` — `CreateSessionRequest` env fields
- [ ] `src/cli/daemon_client.cpp` — serialize env fields + add `GetSessionEnv()` method
- [ ] `src/main.cpp` — `--env-mode`, `-e`, `--env-file` flags for `session start`; `session env {id}` subcommand
- [ ] `src/service/service_install.cpp` / service templates — support explicit
      PATH or env-file injection for launchd/systemd user services
- [ ] Tests:
  - `tests/session/env_resolver_test.cpp` — layer precedence, .env parsing
  - `tests/session/bootstrapped_env_cache_test.cpp` — cache TTL, timeout, parse correctness
  - `tests/net/http_json_test.cpp` — env fields in create request round-trip
  - `tests/service/session_manager_test.cpp` — env mode defaulting logic
  - `tests/service/session_manager_test.cpp` — bootstrap failure is surfaced,
    not flattened into generic session error
  - `tests/net/http_shared_test.cpp` — `GET /sessions/{id}/env` redacts secrets
  - `tests/store/host_config_test.cpp` — host-level env policy persists and
    round-trips correctly
  - `tests/install/service_install_test.cpp` — generated unit/plist includes
    configured PATH/env file entries when requested

---

## Key Constraints

- BootstrapFromShell must NOT hang: hard 5s timeout, fail fast with clear error
- BootstrapFromShell must NOT use a PTY (no `forkpty()`) — avoids triggering
  interactive shell features
- `env -0` output is the required parse format — do not parse human-readable `env`
  output
- `setenv()` in child should be replaced with `execvpe()` envp array — cleaner and
  avoids mutating the daemon's inherited env
- The cache is per daemon lifetime + SIGHUP invalidation — no persistence to disk
- `EffectiveEnvironment` must be stored per `SessionEntry` so it can be returned by
  `GET /sessions/{id}/env` without re-running the bootstrap
- `.env` file with secrets must NOT appear in session snapshots or overview
  WebSocket broadcasts — only the keys (not values) should be visible in
  non-privileged responses
- `GET /sessions/{id}/env` must have a deliberate redaction policy; do not rely
  on route locality alone as the safety boundary
- Bootstrap failure should be explicit by default; a silent fallback to `Clean`
  would hide exactly the class of daemon-vs-shell bugs this feature is meant to
  solve
