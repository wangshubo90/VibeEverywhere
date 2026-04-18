#include "vibe/session/launch_spec.h"

#include <utility>

namespace vibe::session {

auto BuildLaunchSpec(const SessionMetadata& metadata, const ProviderConfig& provider_config,
                     std::vector<std::string> extra_arguments,
                     const TerminalSize terminal_size,
                     EffectiveEnvironment effective_environment) -> LaunchSpec {
  std::vector<std::string> arguments = provider_config.default_args;
  for (auto& argument : extra_arguments) {
    arguments.push_back(std::move(argument));
  }

  return LaunchSpec{
      .provider = metadata.provider,
      .executable = provider_config.executable,
      .arguments = std::move(arguments),
      .effective_environment = std::move(effective_environment),
      .working_directory = metadata.workspace_root,
      .terminal_size = terminal_size,
  };
}

}  // namespace vibe::session
