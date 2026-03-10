#include <gtest/gtest.h>

#include "vibe/session/session_runtime.h"

namespace vibe::session {
namespace {

class FakePtyProcess final : public IPtyProcess {
 public:
  [[nodiscard]] auto Start(const LaunchSpec& launch_spec) -> StartResult override {
    last_launch_spec = launch_spec;
    start_count += 1;
    return next_start_result;
  }

  [[nodiscard]] auto Write(const std::string_view input) -> bool override {
    writes.emplace_back(input);
    return write_result;
  }

  [[nodiscard]] auto Resize(const TerminalSize terminal_size) -> bool override {
    resizes.push_back(terminal_size);
    return resize_result;
  }

  [[nodiscard]] auto Terminate() -> bool override {
    terminate_count += 1;
    return terminate_result;
  }

  StartResult next_start_result{.started = true, .pid = 321, .error_message = ""};
  bool write_result{true};
  bool resize_result{true};
  bool terminate_result{true};
  int start_count{0};
  int terminate_count{0};
  LaunchSpec last_launch_spec{
      .provider = ProviderType::Codex,
      .executable = "",
      .arguments = {},
      .environment_overrides = {},
      .working_directory = "",
      .terminal_size = {},
  };
  std::vector<std::string> writes;
  std::vector<TerminalSize> resizes;
};

auto MakeMetadata() -> SessionMetadata {
  const auto session_id = SessionId::TryCreate("runtime_001");
  EXPECT_TRUE(session_id.has_value());

  return SessionMetadata{
      .id = *session_id,
      .provider = ProviderType::Codex,
      .workspace_root = "/tmp/project",
      .title = "runtime",
      .status = SessionStatus::Created,
  };
}

auto MakeRuntime(FakePtyProcess& pty_process) -> SessionRuntime {
  const SessionMetadata metadata = MakeMetadata();
  const ProviderConfig config = DefaultProviderConfig(metadata.provider);
  const LaunchSpec launch_spec = BuildLaunchSpec(metadata, config, {"hello"});

  return SessionRuntime(SessionRecord(metadata), launch_spec, pty_process);
}

TEST(SessionRuntimeTest, StartTransitionsToRunningAndStoresPid) {
  FakePtyProcess pty_process;
  SessionRuntime runtime = MakeRuntime(pty_process);

  ASSERT_TRUE(runtime.Start());
  EXPECT_EQ(runtime.record().metadata().status, SessionStatus::Running);
  ASSERT_TRUE(runtime.pid().has_value());
  EXPECT_EQ(*runtime.pid(), 321);
  EXPECT_EQ(pty_process.start_count, 1);
  EXPECT_EQ(pty_process.last_launch_spec.arguments, (std::vector<std::string>{"hello"}));
}

TEST(SessionRuntimeTest, FailedStartTransitionsToError) {
  FakePtyProcess pty_process;
  pty_process.next_start_result = StartResult{.started = false, .pid = 0, .error_message = "launch failed"};
  SessionRuntime runtime = MakeRuntime(pty_process);

  EXPECT_FALSE(runtime.Start());
  EXPECT_EQ(runtime.record().metadata().status, SessionStatus::Error);
  EXPECT_FALSE(runtime.pid().has_value());
}

TEST(SessionRuntimeTest, InputAndResizeRequireInteractiveState) {
  FakePtyProcess pty_process;
  SessionRuntime runtime = MakeRuntime(pty_process);

  EXPECT_FALSE(runtime.WriteInput("before start"));
  EXPECT_FALSE(runtime.ResizeTerminal(TerminalSize{.columns = 100, .rows = 40}));

  ASSERT_TRUE(runtime.Start());
  EXPECT_TRUE(runtime.WriteInput("after start"));
  EXPECT_TRUE(runtime.ResizeTerminal(TerminalSize{.columns = 100, .rows = 40}));

  EXPECT_EQ(pty_process.writes, (std::vector<std::string>{"after start"}));
  EXPECT_EQ(pty_process.resizes, (std::vector<TerminalSize>{TerminalSize{.columns = 100, .rows = 40}}));
}

TEST(SessionRuntimeTest, ExitClearsPidAndUpdatesStatus) {
  FakePtyProcess pty_process;
  SessionRuntime runtime = MakeRuntime(pty_process);

  ASSERT_TRUE(runtime.Start());
  ASSERT_TRUE(runtime.MarkAwaitingInput());
  ASSERT_TRUE(runtime.HandleExit(true));

  EXPECT_EQ(runtime.record().metadata().status, SessionStatus::Exited);
  EXPECT_FALSE(runtime.pid().has_value());
}

TEST(SessionRuntimeTest, TerminateDelegatesWhenProcessExists) {
  FakePtyProcess pty_process;
  SessionRuntime runtime = MakeRuntime(pty_process);

  EXPECT_FALSE(runtime.Terminate());
  ASSERT_TRUE(runtime.Start());
  EXPECT_TRUE(runtime.Terminate());
  EXPECT_EQ(pty_process.terminate_count, 1);
}

}  // namespace
}  // namespace vibe::session
