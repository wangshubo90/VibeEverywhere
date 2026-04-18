#ifndef VIBE_SESSION_LAUNCH_SPEC_H
#define VIBE_SESSION_LAUNCH_SPEC_H

#include <cstdint>
#include <string>
#include <vector>

#include "vibe/session/env_config.h"
#include "vibe/session/provider_config.h"
#include "vibe/session/session_types.h"

namespace vibe::session {

struct TerminalSize {
  std::uint16_t columns{120};
  std::uint16_t rows{40};

  [[nodiscard]] auto operator==(const TerminalSize& other) const -> bool = default;
};

struct LaunchSpec {
  ProviderType provider;
  std::string executable;
  std::vector<std::string> arguments;
  // Replaces the old bare environment_overrides map.
  // For LoginShell mode, effective_environment.entries holds session overrides.
  // For Clean/Bootstrap modes, effective_environment holds the full resolved env.
  EffectiveEnvironment effective_environment;
  std::string working_directory;
  TerminalSize terminal_size;
};

[[nodiscard]] auto BuildLaunchSpec(const SessionMetadata& metadata,
                                   const ProviderConfig& provider_config,
                                   std::vector<std::string> extra_arguments = {},
                                   TerminalSize terminal_size = {},
                                   EffectiveEnvironment effective_environment = {}) -> LaunchSpec;

}  // namespace vibe::session

#endif
