#ifndef VIBE_SESSION_LAUNCH_SPEC_H
#define VIBE_SESSION_LAUNCH_SPEC_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

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
  std::unordered_map<std::string, std::string> environment_overrides;
  std::string working_directory;
  TerminalSize terminal_size;
};

[[nodiscard]] auto BuildLaunchSpec(const SessionMetadata& metadata,
                                   const ProviderConfig& provider_config,
                                   std::vector<std::string> extra_arguments = {},
                                   TerminalSize terminal_size = {}) -> LaunchSpec;

}  // namespace vibe::session

#endif
