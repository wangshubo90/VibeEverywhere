#include "vibe/session/posix_pty_process.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <util.h>
#elif defined(__linux__)
#include <pty.h>
#else
#error "PosixPtyProcess is only supported on macOS and Linux"
#endif

#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace vibe::session {
namespace {

class PtyTraceLogger {
 public:
  static auto Instance() -> PtyTraceLogger& {
    static PtyTraceLogger instance;
    return instance;
  }

  void Log(const std::string_view event, const ProcessId pid, const std::size_t value = 0) {
    if (output_ == nullptr) {
      return;
    }

    const auto elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() -
                                                              start_time_)
            .count();
    std::lock_guard<std::mutex> lock(mutex_);
    (*output_) << elapsed << ' ' << pid << ' ' << event << ' ' << value << '\n';
    output_->flush();
  }

 private:
  PtyTraceLogger() {
    const char* path = std::getenv("VIBE_PTY_TRACE_PATH");
    if (path == nullptr || *path == '\0') {
      return;
    }

    output_ = std::make_unique<std::ofstream>(path, std::ios::out | std::ios::trunc);
    if (output_ == nullptr || !output_->is_open()) {
      output_.reset();
      return;
    }
    start_time_ = std::chrono::steady_clock::now();
  }

  std::unique_ptr<std::ofstream> output_;
  std::chrono::steady_clock::time_point start_time_{};
  std::mutex mutex_;
};

auto BuildArgv(const LaunchSpec& launch_spec) -> std::vector<char*> {
  std::vector<char*> argv;
  argv.reserve(launch_spec.arguments.size() + 2);
  argv.push_back(const_cast<char*>(launch_spec.executable.c_str()));

  for (const std::string& argument : launch_spec.arguments) {
    argv.push_back(const_cast<char*>(argument.c_str()));
  }

  argv.push_back(nullptr);
  return argv;
}

void ApplyEnvironmentOverrides(const LaunchSpec& launch_spec) {
  for (const auto& [key, value] : launch_spec.environment_overrides) {
    setenv(key.c_str(), value.c_str(), 1);
  }
}

auto MakeWindowSize(const TerminalSize terminal_size) -> winsize {
  winsize window_size{};
  window_size.ws_col = terminal_size.columns;
  window_size.ws_row = terminal_size.rows;
  return window_size;
}

}  // namespace

PosixPtyProcess::~PosixPtyProcess() {
  const bool terminated = Terminate();
  static_cast<void>(terminated);
  CloseMasterFd();
}

auto PosixPtyProcess::Start(const LaunchSpec& launch_spec) -> StartResult {
  if (master_fd_ != -1 || pid_ != 0) {
    return StartResult{.started = false, .pid = 0, .error_message = "process already started"};
  }

  std::array<int, 2> error_pipe{{-1, -1}};
  if (pipe(error_pipe.data()) != 0) {
    return StartResult{.started = false, .pid = 0, .error_message = strerror(errno)};
  }

  for (int& fd : error_pipe) {
    const int flags = fcntl(fd, F_GETFD);
    if (flags >= 0) {
      const int set_result = fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
      static_cast<void>(set_result);
    }
  }

  winsize window_size = MakeWindowSize(launch_spec.terminal_size);
  pid_t child_pid = forkpty(&master_fd_, nullptr, nullptr, &window_size);

  if (child_pid < 0) {
    const std::string error_message = strerror(errno);
    close(error_pipe[0]);
    close(error_pipe[1]);
    CloseMasterFd();
    return StartResult{.started = false, .pid = 0, .error_message = error_message};
  }

  if (child_pid == 0) {
    close(error_pipe[0]);

    if (chdir(launch_spec.working_directory.c_str()) != 0) {
      const int error_number = errno;
      const auto bytes_written =
          write(error_pipe[1], &error_number, sizeof(error_number));
      static_cast<void>(bytes_written);
      _exit(127);
    }

    ApplyEnvironmentOverrides(launch_spec);
    std::vector<char*> argv = BuildArgv(launch_spec);
    execvp(launch_spec.executable.c_str(), argv.data());

    const int error_number = errno;
    const auto bytes_written = write(error_pipe[1], &error_number, sizeof(error_number));
    static_cast<void>(bytes_written);
    _exit(127);
  }

  close(error_pipe[1]);

  int child_error = 0;
  const ssize_t bytes_read = read(error_pipe[0], &child_error, sizeof(child_error));
  close(error_pipe[0]);

  if (bytes_read > 0) {
    const std::string error_message = strerror(child_error);
    const int waited_pid = waitpid(child_pid, nullptr, 0);
    static_cast<void>(waited_pid);
    CloseMasterFd();
    ResetProcessState();
    return StartResult{.started = false, .pid = 0, .error_message = error_message};
  }

  pid_ = static_cast<ProcessId>(child_pid);
  return StartResult{.started = true, .pid = pid_, .error_message = ""};
}

auto PosixPtyProcess::Write(const std::string_view input) -> bool {
  if (master_fd_ == -1) {
    return false;
  }

  PtyTraceLogger::Instance().Log("pty.write.begin", pid_, input.size());
  const ssize_t bytes_written = write(master_fd_, input.data(), input.size());
  PtyTraceLogger::Instance().Log(bytes_written == static_cast<ssize_t>(input.size()) ? "pty.write.ok"
                                                                                     : "pty.write.fail",
                                 pid_, input.size());
  return bytes_written == static_cast<ssize_t>(input.size());
}

auto PosixPtyProcess::Read(const int timeout_ms) -> ReadResult {
  if (master_fd_ == -1) {
    return ReadResult{.data = "", .closed = true};
  }

  fd_set read_fds;
  FD_ZERO(&read_fds);
  FD_SET(master_fd_, &read_fds);

  timeval timeout{};
  timeout.tv_sec = timeout_ms / 1000;
  timeout.tv_usec = (timeout_ms % 1000) * 1000;

  const int select_result = select(master_fd_ + 1, &read_fds, nullptr, nullptr, &timeout);
  if (select_result <= 0) {
    return ReadResult{.data = "", .closed = false};
  }

  std::array<char, 4096> buffer{};
  const ssize_t bytes_read = read(master_fd_, buffer.data(), buffer.size());
  if (bytes_read == 0) {
    PtyTraceLogger::Instance().Log("pty.read.closed", pid_);
    return ReadResult{.data = "", .closed = true};
  }

  if (bytes_read < 0) {
    if (errno == EIO) {
      PtyTraceLogger::Instance().Log("pty.read.closed", pid_);
      return ReadResult{.data = "", .closed = true};
    }

    return ReadResult{.data = "", .closed = false};
  }

  PtyTraceLogger::Instance().Log("pty.read.data", pid_, static_cast<std::size_t>(bytes_read));

  return ReadResult{
      .data = std::string(buffer.data(), static_cast<std::size_t>(bytes_read)),
      .closed = false,
  };
}

auto PosixPtyProcess::ReadableFd() const -> std::optional<int> {
  if (master_fd_ == -1) {
    return std::nullopt;
  }

  return master_fd_;
}

auto PosixPtyProcess::Resize(const TerminalSize terminal_size) -> bool {
  if (master_fd_ == -1) {
    return false;
  }

  winsize window_size = MakeWindowSize(terminal_size);
  return ioctl(master_fd_, TIOCSWINSZ, &window_size) == 0;
}

auto PosixPtyProcess::PollExit() -> std::optional<int> {
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

  CloseMasterFd();
  pid_ = 0;

  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }

  if (WIFSIGNALED(status)) {
    return 128 + WTERMSIG(status);
  }

  return std::nullopt;
}

auto PosixPtyProcess::Terminate() -> bool {
  if (pid_ == 0) {
    return false;
  }

  const int kill_result = kill(static_cast<pid_t>(pid_), SIGTERM);
  if (kill_result != 0 && errno != ESRCH) {
    return false;
  }

  int status = 0;
  const pid_t wait_result = waitpid(static_cast<pid_t>(pid_), &status, 0);
  if (wait_result < 0 && errno != ECHILD) {
    return false;
  }

  CloseMasterFd();
  pid_ = 0;
  return true;
}

void PosixPtyProcess::CloseMasterFd() {
  if (master_fd_ != -1) {
    close(master_fd_);
    master_fd_ = -1;
  }
}

void PosixPtyProcess::ResetProcessState() { pid_ = 0; }

}  // namespace vibe::session
