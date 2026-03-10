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
  };
}

TEST(LaunchSpecTest, BuildsSpecFromMetadataAndProviderConfig) {
  ProviderConfig config{
      .type = ProviderType::Codex,
      .executable = "codex",
      .default_args = {"--dangerously-bypass-approvals"},
      .environment_overrides = {{"CODEX_ENV", "test"}},
  };

  const LaunchSpec spec =
      BuildLaunchSpec(MakeMetadata(), config, {"prompt", "run tests"}, TerminalSize{.columns = 140, .rows = 50});

  EXPECT_EQ(spec.provider, ProviderType::Codex);
  EXPECT_EQ(spec.executable, "codex");
  EXPECT_EQ(spec.arguments, (std::vector<std::string>{"--dangerously-bypass-approvals", "prompt", "run tests"}));
  EXPECT_EQ(spec.environment_overrides.at("CODEX_ENV"), "test");
  EXPECT_EQ(spec.working_directory, "/tmp/project");
  EXPECT_EQ(spec.terminal_size, (TerminalSize{.columns = 140, .rows = 50}));
}

TEST(LaunchSpecTest, UsesDefaultTerminalSizeWhenNotSpecified) {
  const ProviderConfig config = DefaultProviderConfig(ProviderType::Claude);

  const LaunchSpec spec = BuildLaunchSpec(MakeMetadata(), config);

  EXPECT_EQ(spec.terminal_size, (TerminalSize{}));
  EXPECT_TRUE(spec.arguments.empty());
}

}  // namespace
}  // namespace vibe::session
