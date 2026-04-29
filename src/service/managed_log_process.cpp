#include "vibe/service/managed_log_process.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace vibe::service {
namespace {

constexpr auto kTerminateGracePeriod = std::chrono::milliseconds(1500);
constexpr auto kTerminatePollInterval = std::chrono::milliseconds(25);

auto CurrentUnixTimeMs() -> std::int64_t {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

auto BuildArgv(const vibe::session::LaunchSpec& launch_spec) -> std::vector<char*> {
  std::vector<char*> argv;
  argv.reserve(launch_spec.arguments.size() + 2);
  argv.push_back(const_cast<char*>(launch_spec.executable.c_str()));
  for (const std::string& argument : launch_spec.arguments) {
    argv.push_back(const_cast<char*>(argument.c_str()));
  }
  argv.push_back(nullptr);
  return argv;
}

auto FindEnvValue(const std::vector<char*>& envp, const std::string_view key)
    -> std::optional<std::string_view> {
  const std::string prefix = std::string(key) + "=";
  for (const char* entry : envp) {
    if (entry == nullptr) {
      break;
    }
    const std::string_view value(entry);
    if (value.rfind(prefix, 0) == 0) {
      return value.substr(prefix.size());
    }
  }
  return std::nullopt;
}

auto ResolveExecutablePath(const std::string& executable, const std::vector<char*>& envp)
    -> std::string {
  if (executable.find('/') != std::string::npos) {
    return executable;
  }

  const std::string_view path_value =
      FindEnvValue(envp, "PATH").value_or(std::string_view("/usr/bin:/bin:/usr/sbin:/sbin"));
  std::size_t start = 0;
  while (start <= path_value.size()) {
    const std::size_t end = path_value.find(':', start);
    const std::string_view directory =
        end == std::string_view::npos ? path_value.substr(start)
                                      : path_value.substr(start, end - start);
    const std::string candidate =
        (directory.empty() ? std::string(".") : std::string(directory)) + "/" + executable;
    if (access(candidate.c_str(), X_OK) == 0) {
      return candidate;
    }
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }

  return executable;
}

auto BuildEnvp(const vibe::session::LaunchSpec& launch_spec,
               std::vector<std::string>& env_strings) -> std::vector<char*> {
  if (launch_spec.effective_environment.mode == vibe::session::EnvMode::Shell) {
    return {};
  }

  env_strings.clear();
  env_strings.reserve(launch_spec.effective_environment.entries.size());
  std::vector<char*> envp;
  envp.reserve(launch_spec.effective_environment.entries.size() + 1);
  for (const auto& entry : launch_spec.effective_environment.entries) {
    env_strings.push_back(entry.key + "=" + entry.value);
    envp.push_back(env_strings.back().data());
  }
  envp.push_back(nullptr);
  return envp;
}

auto SetCloseOnExec(const int fd) -> bool {
  const int flags = fcntl(fd, F_GETFD);
  if (flags < 0) {
    return false;
  }
  return fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
}

auto WaitForProcessExit(const std::int64_t pid, const std::chrono::milliseconds timeout)
    -> std::optional<int> {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    int status = 0;
    const pid_t wait_result = waitpid(static_cast<pid_t>(pid), &status, WNOHANG);
    if (wait_result == static_cast<pid_t>(pid)) {
      return status;
    }
    if (wait_result < 0) {
      if (errno == ECHILD) {
        return 0;
      }
      return std::nullopt;
    }
    std::this_thread::sleep_for(kTerminatePollInterval);
  }
  return std::nullopt;
}

auto CloseFd(int& fd) -> void {
  if (fd != -1) {
    close(fd);
    fd = -1;
  }
}

}  // namespace

ManagedLogProcess::~ManagedLogProcess() {
  const bool terminated = Terminate();
  static_cast<void>(terminated);
}

auto ManagedLogProcess::Start(const vibe::session::LaunchSpec& launch_spec,
                              OutputCallback output_callback) -> ManagedLogProcessStartResult {
  if (pid_ != 0) {
    return ManagedLogProcessStartResult{
        .started = false,
        .pid = 0,
        .error_message = "process already started",
    };
  }

  std::array<int, 2> stdout_pipe{{-1, -1}};
  std::array<int, 2> stderr_pipe{{-1, -1}};
  std::array<int, 2> error_pipe{{-1, -1}};
  if (pipe(stdout_pipe.data()) != 0 || pipe(stderr_pipe.data()) != 0 ||
      pipe(error_pipe.data()) != 0) {
    const std::string error_message = strerror(errno);
    CloseFd(stdout_pipe[0]);
    CloseFd(stdout_pipe[1]);
    CloseFd(stderr_pipe[0]);
    CloseFd(stderr_pipe[1]);
    CloseFd(error_pipe[0]);
    CloseFd(error_pipe[1]);
    return ManagedLogProcessStartResult{
        .started = false,
        .pid = 0,
        .error_message = error_message,
    };
  }

  if (!SetCloseOnExec(error_pipe[0]) || !SetCloseOnExec(error_pipe[1])) {
    const std::string error_message = strerror(errno);
    CloseFd(stdout_pipe[0]);
    CloseFd(stdout_pipe[1]);
    CloseFd(stderr_pipe[0]);
    CloseFd(stderr_pipe[1]);
    CloseFd(error_pipe[0]);
    CloseFd(error_pipe[1]);
    return ManagedLogProcessStartResult{
        .started = false,
        .pid = 0,
        .error_message = error_message,
    };
  }

  const pid_t child_pid = fork();
  if (child_pid < 0) {
    const std::string error_message = strerror(errno);
    CloseFd(stdout_pipe[0]);
    CloseFd(stdout_pipe[1]);
    CloseFd(stderr_pipe[0]);
    CloseFd(stderr_pipe[1]);
    CloseFd(error_pipe[0]);
    CloseFd(error_pipe[1]);
    return ManagedLogProcessStartResult{
        .started = false,
        .pid = 0,
        .error_message = error_message,
    };
  }

  if (child_pid == 0) {
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);
    close(error_pipe[0]);

    if (dup2(stdout_pipe[1], STDOUT_FILENO) < 0 ||
        dup2(stderr_pipe[1], STDERR_FILENO) < 0) {
      const int error_number = errno;
      const auto bytes_written = write(error_pipe[1], &error_number, sizeof(error_number));
      static_cast<void>(bytes_written);
      _exit(127);
    }
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    if (chdir(launch_spec.working_directory.c_str()) != 0) {
      const int error_number = errno;
      const auto bytes_written = write(error_pipe[1], &error_number, sizeof(error_number));
      static_cast<void>(bytes_written);
      _exit(127);
    }

    std::vector<std::string> env_strings;
    const std::vector<char*> envp = BuildEnvp(launch_spec, env_strings);
    std::vector<char*> argv = BuildArgv(launch_spec);
    if (envp.empty()) {
      execvp(launch_spec.executable.c_str(), argv.data());
    } else {
      const std::string resolved_executable = ResolveExecutablePath(launch_spec.executable, envp);
      execve(resolved_executable.c_str(), argv.data(), const_cast<char* const*>(envp.data()));
    }

    const int error_number = errno;
    const auto bytes_written = write(error_pipe[1], &error_number, sizeof(error_number));
    static_cast<void>(bytes_written);
    _exit(127);
  }

  close(stdout_pipe[1]);
  stdout_pipe[1] = -1;
  close(stderr_pipe[1]);
  stderr_pipe[1] = -1;
  close(error_pipe[1]);
  error_pipe[1] = -1;

  int child_error = 0;
  ssize_t bytes_read = 0;
  do {
    bytes_read = read(error_pipe[0], &child_error, sizeof(child_error));
  } while (bytes_read < 0 && errno == EINTR);
  CloseFd(error_pipe[0]);
  if (bytes_read != 0) {
    const std::string error_message =
        bytes_read > 0 ? strerror(child_error) : strerror(errno);
    const int waited_pid = waitpid(child_pid, nullptr, 0);
    static_cast<void>(waited_pid);
    CloseFd(stdout_pipe[0]);
    CloseFd(stderr_pipe[0]);
    return ManagedLogProcessStartResult{
        .started = false,
        .pid = 0,
        .error_message = error_message,
    };
  }

  pid_ = static_cast<std::int64_t>(child_pid);
  output_callback_ = std::move(output_callback);
  StartReader(stdout_pipe[0], LogStream::Stdout);
  StartReader(stderr_pipe[0], LogStream::Stderr);
  return ManagedLogProcessStartResult{
      .started = true,
      .pid = pid_,
      .error_message = "",
  };
}

auto ManagedLogProcess::PollExit() -> std::optional<int> {
  if (pid_ == 0) {
    return std::nullopt;
  }

  int status = 0;
  const pid_t wait_result = waitpid(static_cast<pid_t>(pid_), &status, WNOHANG);
  if (wait_result == 0) {
    return std::nullopt;
  }
  if (wait_result < 0) {
    return std::nullopt;
  }

  pid_ = 0;
  JoinReaders();
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  if (WIFSIGNALED(status)) {
    return 128 + WTERMSIG(status);
  }
  return std::nullopt;
}

auto ManagedLogProcess::Terminate() -> bool {
  if (pid_ == 0) {
    JoinReaders();
    return true;
  }

  const int kill_result = kill(static_cast<pid_t>(pid_), SIGTERM);
  if (kill_result != 0 && errno != ESRCH) {
    return false;
  }

  if (!WaitForProcessExit(pid_, kTerminateGracePeriod).has_value()) {
    const int kill_fallback_result = kill(static_cast<pid_t>(pid_), SIGKILL);
    if (kill_fallback_result != 0 && errno != ESRCH) {
      return false;
    }
    int status = 0;
    const pid_t wait_result = waitpid(static_cast<pid_t>(pid_), &status, 0);
    if (wait_result < 0 && errno != ECHILD) {
      return false;
    }
  }

  pid_ = 0;
  JoinReaders();
  return true;
}

auto ManagedLogProcess::pid() const -> std::int64_t {
  return pid_;
}

void ManagedLogProcess::JoinReaders() {
  if (stdout_reader_.joinable()) {
    stdout_reader_.join();
  }
  if (stderr_reader_.joinable()) {
    stderr_reader_.join();
  }
}

void ManagedLogProcess::StartReader(const int fd, const LogStream stream) {
  std::thread& reader = stream == LogStream::Stdout ? stdout_reader_ : stderr_reader_;
  reader = std::thread([this, fd, stream]() {
    std::array<char, 4096> buffer{};
    while (true) {
      const ssize_t bytes_read = read(fd, buffer.data(), buffer.size());
      if (bytes_read > 0) {
        if (output_callback_) {
          output_callback_(stream, std::string(buffer.data(), static_cast<std::size_t>(bytes_read)),
                           CurrentUnixTimeMs());
        }
        continue;
      }
      if (bytes_read < 0 && errno == EINTR) {
        continue;
      }
      break;
    }
    close(fd);
  });
}

}  // namespace vibe::service
