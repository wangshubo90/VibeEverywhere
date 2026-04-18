#include "env_resolver.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "env_file_parser.h"
#include "vibe/session/env_config.h"

namespace vibe::session {

namespace {

constexpr std::string_view kMinimalPath = "/usr/bin:/bin:/usr/sbin:/sbin";

auto Getenv(const std::string_view name) -> std::string {
  const char* value = std::getenv(std::string(name).c_str());
  return value != nullptr ? std::string(value) : std::string{};
}

// Load a .env file from disk and return parsed pairs.
auto LoadEnvFile(const std::string& path,
                 const std::unordered_map<std::string, std::string>& current_env)
    -> std::vector<std::pair<std::string, std::string>> {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return {};
  }
  std::ostringstream buf;
  buf << file.rdbuf();
  return ParseEnvFile(buf.str(), current_env);
}

// Find .env file to load: explicit path, or workspace_root/.env.in then workspace_root/.env.
auto ResolveEnvFilePaths(const std::optional<std::string>& explicit_path,
                          const std::string_view workspace_root)
    -> std::vector<std::string> {
  if (explicit_path.has_value()) {
    std::filesystem::path p(*explicit_path);
    if (p.is_relative()) {
      p = std::filesystem::path(workspace_root) / p;
    }
    return {p.string()};
  }
  // Defaults: workspace/.env.in first (template), then workspace/.env (overrides).
  std::vector<std::string> paths;
  const std::filesystem::path ws(workspace_root);
  const auto env_in = (ws / ".env.in").string();
  const auto env_file = (ws / ".env").string();
  if (std::filesystem::exists(env_in)) {
    paths.push_back(env_in);
  }
  if (std::filesystem::exists(env_file)) {
    paths.push_back(env_file);
  }
  return paths;
}

void ApplyOverrides(std::vector<EnvEntry>& entries,
                    const std::unordered_map<std::string, std::string>& overrides,
                    const EnvSource source) {
  // Build index of existing keys for efficient lookup.
  std::unordered_map<std::string, std::size_t> key_index;
  key_index.reserve(entries.size());
  for (std::size_t i = 0; i < entries.size(); ++i) {
    key_index[entries[i].key] = i;
  }

  for (const auto& [key, value] : overrides) {
    const auto it = key_index.find(key);
    if (it != key_index.end()) {
      entries[it->second].value = value;
      entries[it->second].source = source;
    } else {
      const std::size_t idx = entries.size();
      entries.push_back(EnvEntry{.key = key, .value = value, .source = source});
      key_index[key] = idx;
    }
  }
}

// Build a flat map from entries (last write wins).
auto EntriesToMap(const std::vector<EnvEntry>& entries)
    -> std::unordered_map<std::string, std::string> {
  std::unordered_map<std::string, std::string> m;
  m.reserve(entries.size());
  for (const auto& e : entries) {
    m[e.key] = e.value;
  }
  return m;
}

}  // namespace

auto ResolveBootstrapShell(const vibe::store::HostIdentity& host_config) -> std::string {
  if (host_config.bootstrap_shell_path.has_value() &&
      !host_config.bootstrap_shell_path->empty()) {
    return *host_config.bootstrap_shell_path;
  }
  const std::string shell_env = Getenv("SHELL");
  if (!shell_env.empty()) {
    return shell_env;
  }
#ifdef __APPLE__
  return "/bin/zsh";
#else
  return "/bin/bash";
#endif
}

auto ResolveEnvironment(const EnvConfig& config,
                        const std::string_view workspace_root,
                        const vibe::store::HostIdentity& host_config,
                        BootstrappedEnvCache& cache,
                        const std::unordered_map<std::string, std::string>& provider_overrides)
    -> EffectiveEnvResult {
  EffectiveEnvironment result;
  result.mode = config.mode;

  if (config.mode == EnvMode::LoginShell) {
    // LoginShell: the session IS a login shell. No pre-capture needed.
    // Overrides applied via setenv() in posix_pty_process (legacy path).
    // We just record what was requested; env application is done at exec time.
    for (const auto& [k, v] : config.overrides) {
      result.entries.push_back(EnvEntry{.key = k, .value = v, .source = EnvSource::SessionOverride});
    }
    return EffectiveEnvResult::Ok(std::move(result));
  }

  if (config.mode == EnvMode::Clean) {
    // Layer 1: minimal base vars.
    const std::string home = Getenv("HOME");
    const std::string user = Getenv("USER");
    const std::string tmpdir = Getenv("TMPDIR");

    result.entries.push_back(
        EnvEntry{.key = "PATH", .value = std::string(kMinimalPath), .source = EnvSource::DaemonInherited});
    if (!home.empty()) {
      result.entries.push_back(
          EnvEntry{.key = "HOME", .value = home, .source = EnvSource::DaemonInherited});
    }
    if (!user.empty()) {
      result.entries.push_back(
          EnvEntry{.key = "USER", .value = user, .source = EnvSource::DaemonInherited});
    }
    if (!tmpdir.empty()) {
      result.entries.push_back(
          EnvEntry{.key = "TMPDIR", .value = tmpdir, .source = EnvSource::DaemonInherited});
    }

    // Layer 2: .env file
    const auto env_file_paths = ResolveEnvFilePaths(config.env_file_path, workspace_root);
    for (const auto& path : env_file_paths) {
      const auto current_map = EntriesToMap(result.entries);
      const auto pairs = LoadEnvFile(path, current_map);
      for (const auto& [k, v] : pairs) {
        // Check if key exists, update or append.
        bool found = false;
        for (auto& entry : result.entries) {
          if (entry.key == k) {
            entry.value = v;
            entry.source = EnvSource::EnvFile;
            found = true;
            break;
          }
        }
        if (!found) {
          result.entries.push_back(EnvEntry{.key = k, .value = v, .source = EnvSource::EnvFile});
        }
      }
      if (!env_file_paths.empty()) {
        result.env_file_path = path;
      }
    }
    if (!env_file_paths.empty()) {
      result.env_file_path = env_file_paths.back();
    }

    // Layer 3: ProviderConfig overrides.
    ApplyOverrides(result.entries, provider_overrides, EnvSource::ProviderConfig);

    // Layer 4: session-level overrides.
    ApplyOverrides(result.entries, config.overrides, EnvSource::SessionOverride);

    return EffectiveEnvResult::Ok(std::move(result));
  }

  // EnvMode::BootstrapFromShell
  const std::string shell_path = ResolveBootstrapShell(host_config);
  result.bootstrap_shell_path = shell_path;

  auto bootstrap_result = cache.Get(shell_path);
  if (!bootstrap_result.has_value()) {
    return EffectiveEnvResult::Err(bootstrap_result.error());
  }
  result.bootstrap_warning = cache.TakeLastWarning();

  // Layer 1: bootstrap-captured env.
  result.entries.reserve(bootstrap_result.value().size());
  for (const auto& [k, v] : bootstrap_result.value()) {
    result.entries.push_back(EnvEntry{.key = k, .value = v, .source = EnvSource::BootstrapShell});
  }

  // Layer 2: service-manager imported env (if enabled by HostConfig, allowlist only).
  if (host_config.import_service_manager_environment &&
      !host_config.service_manager_environment_allowlist.empty()) {
    for (const auto& key : host_config.service_manager_environment_allowlist) {
      const char* env_val = std::getenv(key.c_str());
      if (env_val == nullptr) {
        continue;
      }
      const std::string value(env_val);
      bool found = false;
      for (auto& entry : result.entries) {
        if (entry.key == key) {
          entry.value = value;
          entry.source = EnvSource::ServiceManager;
          found = true;
          break;
        }
      }
      if (!found) {
        result.entries.push_back(
            EnvEntry{.key = key, .value = value, .source = EnvSource::ServiceManager});
      }
    }
  }

  // Layer 3: .env file overrides.
  const auto env_file_paths = ResolveEnvFilePaths(config.env_file_path, workspace_root);
  for (const auto& path : env_file_paths) {
    const auto current_map = EntriesToMap(result.entries);
    const auto pairs = LoadEnvFile(path, current_map);
    for (const auto& [k, v] : pairs) {
      bool found = false;
      for (auto& entry : result.entries) {
        if (entry.key == k) {
          entry.value = v;
          entry.source = EnvSource::EnvFile;
          found = true;
          break;
        }
      }
      if (!found) {
        result.entries.push_back(EnvEntry{.key = k, .value = v, .source = EnvSource::EnvFile});
      }
    }
  }
  if (!env_file_paths.empty()) {
    result.env_file_path = env_file_paths.back();
  }

  // Layer 4: ProviderConfig overrides.
  ApplyOverrides(result.entries, provider_overrides, EnvSource::ProviderConfig);

  // Layer 5: session-level overrides.
  ApplyOverrides(result.entries, config.overrides, EnvSource::SessionOverride);

  return EffectiveEnvResult::Ok(std::move(result));
}

}  // namespace vibe::session
