#include "vibe/session/env_config.h"

namespace vibe::session {

auto ToString(const EnvMode mode) -> std::string_view {
  switch (mode) {
    case EnvMode::Clean:
      return "clean";
    case EnvMode::LoginShell:
      return "login_shell";
    case EnvMode::BootstrapFromShell:
      return "bootstrap_from_shell";
  }
  return "bootstrap_from_shell";
}

auto ParseEnvMode(const std::string_view s) -> std::optional<EnvMode> {
  if (s == "clean") {
    return EnvMode::Clean;
  }
  if (s == "login_shell" || s == "login-shell") {
    return EnvMode::LoginShell;
  }
  if (s == "bootstrap_from_shell" || s == "bootstrap" || s == "bootstrap-from-shell") {
    return EnvMode::BootstrapFromShell;
  }
  return std::nullopt;
}

auto ToString(const EnvSource source) -> std::string_view {
  switch (source) {
    case EnvSource::DaemonInherited:
      return "daemon_inherited";
    case EnvSource::ServiceManager:
      return "service_manager";
    case EnvSource::BootstrapShell:
      return "bootstrap_shell";
    case EnvSource::EnvFile:
      return "env_file";
    case EnvSource::ProviderConfig:
      return "provider_config";
    case EnvSource::SessionOverride:
      return "session_override";
  }
  return "daemon_inherited";
}

}  // namespace vibe::session
