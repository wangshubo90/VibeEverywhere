#ifndef VIBE_SESSION_ENV_CONFIG_H
#define VIBE_SESSION_ENV_CONFIG_H

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vibe::session {

enum class EnvMode {
  Clean,               // Only base vars + explicit overrides + .env file
  LoginShell,          // Session IS a login shell (zsh -l or bash -l)
  BootstrapFromShell,  // Capture env from login shell once, apply to child
};

[[nodiscard]] auto ToString(EnvMode mode) -> std::string_view;
[[nodiscard]] auto ParseEnvMode(std::string_view s) -> std::optional<EnvMode>;

struct EnvConfig {
  EnvMode mode = EnvMode::BootstrapFromShell;

  // Per-session overrides. Applied on top of whatever the mode produces.
  // Highest precedence -- always wins.
  std::unordered_map<std::string, std::string> overrides;

  // Path to .env file. Relative paths resolved from workspace_root.
  // Optional -- if absent, defaults to workspace_root/.env if it exists.
  std::optional<std::string> env_file_path;
};

// Source tag for each variable -- used for debug/inspect output.
enum class EnvSource {
  DaemonInherited,  // Came from daemon's own environment (base layer)
  ServiceManager,   // Imported via launchd/systemd user-service config
  BootstrapShell,   // Captured from login shell bootstrap
  EnvFile,          // Loaded from .env or .env.in file
  ProviderConfig,   // Set by ProviderConfig::environment_overrides
  SessionOverride,  // Explicitly set by session request or CLI -e flag
};

[[nodiscard]] auto ToString(EnvSource source) -> std::string_view;

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

#endif
