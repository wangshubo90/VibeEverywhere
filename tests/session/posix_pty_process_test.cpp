#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>

#include "vibe/session/posix_pty_process.h"

namespace vibe::session {
namespace {

auto MakeShellLaunchSpec() -> LaunchSpec {
  return LaunchSpec{
      .provider = ProviderType::Codex,
      .executable = "/bin/sh",
      .arguments = {"-c", "printf 'ready\\n'; IFS= read -r line; printf 'echo:%s\\n' \"$line\""},
      .effective_environment = {},
      .working_directory = "/tmp",
      .terminal_size = TerminalSize{.columns = 80, .rows = 24},
  };
}

auto ReadUntilContains(PosixPtyProcess& process, const std::string& needle,
                       const std::chrono::milliseconds timeout) -> std::string {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  std::string collected;

  while (std::chrono::steady_clock::now() < deadline) {
    const ReadResult result = process.Read(100);
    collected += result.data;
    if (collected.find(needle) != std::string::npos) {
      return collected;
    }
    if (result.closed) {
      return collected;
    }
  }

  return collected;
}

TEST(PosixPtyProcessTest, StartsWritesReadsAndObservesExit) {
  PosixPtyProcess process;

  const StartResult start_result = process.Start(MakeShellLaunchSpec());
  ASSERT_TRUE(start_result.started) << start_result.error_message;
  EXPECT_GT(start_result.pid, 0);

  const std::string initial_output =
      ReadUntilContains(process, "ready", std::chrono::milliseconds(2000));
  EXPECT_NE(initial_output.find("ready"), std::string::npos);

  ASSERT_TRUE(process.Write("hello world\n"));

  const std::string echoed_output =
      ReadUntilContains(process, "echo:hello world", std::chrono::milliseconds(2000));
  EXPECT_NE(echoed_output.find("echo:hello world"), std::string::npos);

  std::optional<int> exit_code;
  for (int attempt = 0; attempt < 20 && !exit_code.has_value(); ++attempt) {
    exit_code = process.PollExit();
    if (!exit_code.has_value()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }

  ASSERT_TRUE(exit_code.has_value());
  EXPECT_EQ(*exit_code, 0);
}

TEST(PosixPtyProcessTest, FailsToStartWithInvalidWorkingDirectory) {
  PosixPtyProcess process;
  LaunchSpec launch_spec = MakeShellLaunchSpec();
  launch_spec.working_directory = "/path/does/not/exist";

  const StartResult start_result = process.Start(launch_spec);
  EXPECT_FALSE(start_result.started);
  EXPECT_FALSE(start_result.error_message.empty());
}

}  // namespace
}  // namespace vibe::session
