#include <gtest/gtest.h>

#include "vibe/session/launch_spec.h"

namespace vibe::session {
namespace {

auto MakeMetadata() -> SessionMetadata {
  const auto session_id = SessionId::TryCreate("launch_001");
  EXPECT_TRUE(session_id.has_value());

  return SessionMetadata{
      .id = *session_id,
      .provider = ProviderType::Codex,
      .workspace_root = "/tmp/project",
      .title = "launch-spec",
      .status = SessionStatus::Created,
      .conversation_id = std::nullopt,
      .group_tags = {},
  };
}

TEST(LaunchSpecTest, BuildsSpecFromMetadataAndProviderConfig) {
  ProviderConfig config{
      .type = ProviderType::Codex,
      .executable = "codex",
      .default_args = {"--dangerously-bypass-approvals"},
      .environment_overrides = {{"CODEX_ENV", "test"}},
  };

  // Build a pre-resolved EffectiveEnvironment that carries the provider override.
  EffectiveEnvironment effective_env;
  effective_env.entries.push_back(
      EnvEntry{.key = "CODEX_ENV", .value = "test", .source = EnvSource::ProviderConfig});

  const LaunchSpec spec = BuildLaunchSpec(MakeMetadata(), config, {"prompt", "run tests"},
                                          TerminalSize{.columns = 140, .rows = 50},
                                          std::move(effective_env));

  EXPECT_EQ(spec.provider, ProviderType::Codex);
  EXPECT_EQ(spec.executable, "codex");
  EXPECT_EQ(spec.arguments, (std::vector<std::string>{"--dangerously-bypass-approvals", "prompt", "run tests"}));
  // Provider overrides are now inside effective_environment.entries.
  ASSERT_EQ(spec.effective_environment.entries.size(), 1u);
  EXPECT_EQ(spec.effective_environment.entries[0].key, "CODEX_ENV");
  EXPECT_EQ(spec.effective_environment.entries[0].value, "test");
  EXPECT_EQ(spec.working_directory, "/tmp/project");
  EXPECT_EQ(spec.terminal_size, (TerminalSize{.columns = 140, .rows = 50}));
}

TEST(LaunchSpecTest, UsesDefaultTerminalSizeWhenNotSpecified) {
  const ProviderConfig config = DefaultProviderConfig(ProviderType::Claude);

  const LaunchSpec spec = BuildLaunchSpec(MakeMetadata(), config);

  EXPECT_EQ(spec.terminal_size, (TerminalSize{}));
  EXPECT_TRUE(spec.arguments.empty());
}

TEST(LaunchSpecTest, SupportsExplicitCommandOverrideByProviderConfigMutation) {
  ProviderConfig config = DefaultProviderConfig(ProviderType::Claude);
  config.executable = "/custom/claude";
  config.default_args = {"--model", "sonnet"};

  const LaunchSpec spec = BuildLaunchSpec(MakeMetadata(), config);

  EXPECT_EQ(spec.executable, "/custom/claude");
  EXPECT_EQ(spec.arguments, (std::vector<std::string>{"--model", "sonnet"}));
}

}  // namespace
}  // namespace vibe::session
