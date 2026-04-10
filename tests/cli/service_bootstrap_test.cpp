#include <gtest/gtest.h>

#include <sys/wait.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace vibe::cli {
namespace {

struct CommandResult {
  int exit_code{-1};
  std::string output;
};

auto ReadFile(const std::filesystem::path& path) -> std::string {
  std::ifstream input(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

auto RunSentritsCommand(const std::vector<std::string>& args, const std::filesystem::path& home_dir,
                        const std::optional<std::string>& web_root = std::nullopt) -> CommandResult {
  int pipe_fds[2];
  EXPECT_EQ(pipe(pipe_fds), 0);

  const pid_t pid = fork();
  EXPECT_GE(pid, 0);
  if (pid == 0) {
    close(pipe_fds[0]);
    dup2(pipe_fds[1], STDOUT_FILENO);
    dup2(pipe_fds[1], STDERR_FILENO);
    close(pipe_fds[1]);

    setenv("HOME", home_dir.c_str(), 1);
    if (web_root.has_value()) {
      setenv("SENTRITS_WEB_ROOT", web_root->c_str(), 1);
    } else {
      unsetenv("SENTRITS_WEB_ROOT");
    }

    std::vector<char*> argv;
    argv.reserve(args.size() + 2U);
    argv.push_back(const_cast<char*>(VIBE_HOSTD_PATH));
    for (const auto& arg : args) {
      argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    execv(VIBE_HOSTD_PATH, argv.data());
    _exit(127);
  }

  close(pipe_fds[1]);
  std::string output;
  char buffer[4096];
  while (true) {
    const ssize_t bytes_read = read(pipe_fds[0], buffer, sizeof(buffer));
    if (bytes_read <= 0) {
      break;
    }
    output.append(buffer, static_cast<std::size_t>(bytes_read));
  }
  close(pipe_fds[0]);

  int status = 0;
  EXPECT_EQ(waitpid(pid, &status, 0), pid);

  CommandResult result;
  if (WIFEXITED(status)) {
    result.exit_code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    result.exit_code = 128 + WTERMSIG(status);
  }
  result.output = std::move(output);
  return result;
}

auto MakeTempHome(const std::string& suffix) -> std::filesystem::path {
  const auto path = std::filesystem::temp_directory_path() / suffix;
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}

TEST(ServiceBootstrapTest, PrintEmitsPlatformServiceDefinition) {
  const auto home_dir = MakeTempHome("sentrits-service-print-home");
  const auto result = RunSentritsCommand({"service", "print"}, home_dir, "/tmp/sentrits-test-www");

  EXPECT_EQ(result.exit_code, 0);
#if defined(__APPLE__)
  EXPECT_NE(result.output.find("# path: " + (home_dir / "Library" / "LaunchAgents" / "io.sentrits.agent.plist").string()),
            std::string::npos);
  EXPECT_NE(result.output.find("<string>serve</string>"), std::string::npos);
  EXPECT_NE(result.output.find("<key>SENTRITS_WEB_ROOT</key>"), std::string::npos);
  EXPECT_NE(result.output.find("/tmp/sentrits-test-www"), std::string::npos);
#else
  EXPECT_NE(result.output.find("# path: " + (home_dir / ".config" / "systemd" / "user" / "sentrits.service").string()),
            std::string::npos);
  EXPECT_NE(result.output.find("ExecStart="), std::string::npos);
  EXPECT_NE(result.output.find("serve --admin-host 127.0.0.1 --remote-host 0.0.0.0"), std::string::npos);
  EXPECT_NE(result.output.find("Environment=SENTRITS_WEB_ROOT=/tmp/sentrits-test-www"), std::string::npos);
#endif

  std::filesystem::remove_all(home_dir);
}

TEST(ServiceBootstrapTest, InstallWritesServiceFileUnderUserHome) {
  const auto home_dir = MakeTempHome("sentrits-service-install-home");
  const auto result = RunSentritsCommand({"service", "install"}, home_dir, "/tmp/sentrits-install-www");

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_NE(result.output.find("installed service file:"), std::string::npos);

#if defined(__APPLE__)
  const auto service_path = home_dir / "Library" / "LaunchAgents" / "io.sentrits.agent.plist";
  ASSERT_TRUE(std::filesystem::exists(service_path));
  const std::string content = ReadFile(service_path);
  EXPECT_NE(content.find("<string>io.sentrits.agent</string>"), std::string::npos);
  EXPECT_NE(content.find("<string>--remote-host</string>"), std::string::npos);
  EXPECT_NE(content.find("<string>0.0.0.0</string>"), std::string::npos);
  EXPECT_NE(content.find("/tmp/sentrits-install-www"), std::string::npos);
  EXPECT_NE(result.output.find("launchctl load"), std::string::npos);
#else
  const auto service_path = home_dir / ".config" / "systemd" / "user" / "sentrits.service";
  ASSERT_TRUE(std::filesystem::exists(service_path));
  const std::string content = ReadFile(service_path);
  EXPECT_NE(content.find("Description=Sentrits user session daemon"), std::string::npos);
  EXPECT_NE(content.find("serve --admin-host 127.0.0.1 --remote-host 0.0.0.0"), std::string::npos);
  EXPECT_NE(content.find("Environment=SENTRITS_WEB_ROOT=/tmp/sentrits-install-www"), std::string::npos);
  EXPECT_NE(result.output.find("systemctl --user daemon-reload"), std::string::npos);
#endif

  std::filesystem::remove_all(home_dir);
}

}  // namespace
}  // namespace vibe::cli
