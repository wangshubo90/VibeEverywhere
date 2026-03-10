#include "vibe/session/launch_spec.h"

#include <utility>

namespace vibe::session {

auto BuildLaunchSpec(const SessionMetadata& metadata, const ProviderConfig& provider_config,
                     std::vector<std::string> extra_arguments,
                     const TerminalSize terminal_size) -> LaunchSpec {
  std::vector<std::string> arguments = provider_config.default_args;
  for (auto& argument : extra_arguments) {
    arguments.push_back(std::move(argument));
  }

  return LaunchSpec{
      .provider = metadata.provider,
      .executable = provider_config.executable,
      .arguments = std::move(arguments),
      .environment_overrides = provider_config.environment_overrides,
      .working_directory = metadata.workspace_root,
      .terminal_size = terminal_size,
  };
}

}  // namespace vibe::session
