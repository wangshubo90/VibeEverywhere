#include "vibe/session/bootstrapped_env_cache.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vibe::session {

namespace {

constexpr std::string_view kBootstrapCommand = "command -p env -0";
constexpr int kBootstrapTimeoutSeconds = 5;

// Keys to strip from bootstrapped env -- shell-internal noise.
constexpr std::array<std::string_view, 4> kStripKeys = {"SHLVL", "_", "PWD", "OLDPWD"};

auto ShouldStrip(const std::string_view key) -> bool {
  return std::ranges::any_of(kStripKeys, [key](std::string_view k) { return k == key; });
}

auto ParseEnvZero(const std::string& output) -> std::unordered_map<std::string, std::string> {
  std::unordered_map<std::string, std::string> result;
  std::size_t pos = 0;
  while (pos < output.size()) {
    const std::size_t nul = output.find('\0', pos);
    const std::size_t end = (nul == std::string::npos) ? output.size() : nul;
    if (end > pos) {
      const std::string_view token(output.data() + pos, end - pos);
      const std::size_t eq = token.find('=');
      if (eq != std::string_view::npos && eq > 0) {
        const std::string key(token.substr(0, eq));
        const std::string value(token.substr(eq + 1));
        if (!ShouldStrip(key)) {
          result[key] = value;
        }
      }
    }
    if (nul == std::string::npos) {
      break;
    }
    pos = nul + 1;
  }
  return result;
}

}  // namespace

auto BootstrappedEnvCache::RunBootstrap(const std::string& shell_path) -> EnvMapResult {
  last_warning_.reset();
  // Pipes: stdout (env -0 output) and stderr (warnings/errors).
  std::array<int, 2> stdout_pipe{{-1, -1}};
  std::array<int, 2> stderr_pipe{{-1, -1}};

  if (pipe(stdout_pipe.data()) != 0) {
    return EnvMapResult::Err(std::string("pipe() failed: ") + strerror(errno));
  }
  if (pipe(stderr_pipe.data()) != 0) {
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    return EnvMapResult::Err(std::string("pipe() failed: ") + strerror(errno));
  }

  const pid_t child_pid = fork();
  if (child_pid < 0) {
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[0]);
    close(stderr_pipe[1]);
    return EnvMapResult::Err(std::string("fork() failed: ") + strerror(errno));
  }

  if (child_pid == 0) {
    // Child: redirect stdout and stderr to pipes, then exec shell.
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    if (dup2(stdout_pipe[1], STDOUT_FILENO) < 0) {
      _exit(127);
    }
    if (dup2(stderr_pipe[1], STDERR_FILENO) < 0) {
      _exit(127);
    }
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    // Build: <shell> -l -c 'command -p env -0'
    const char* args[] = {
        shell_path.c_str(),
        "-l",
        "-c",
        kBootstrapCommand.data(),
        nullptr,
    };
    execvp(shell_path.c_str(), const_cast<char* const*>(args));
    _exit(127);
  }

  // Parent: close write ends.
  close(stdout_pipe[1]);
  close(stderr_pipe[1]);

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(kBootstrapTimeoutSeconds);

  // Read stdout into stdout_data using a deadline-aware loop.
  std::string stdout_data;
  std::string stderr_data;
  bool stdout_done = false;
  bool stderr_done = false;

  std::array<char, 4096> buffer{};
  while (!stdout_done || !stderr_done) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      kill(child_pid, SIGKILL);
      waitpid(child_pid, nullptr, 0);
      close(stdout_pipe[0]);
      close(stderr_pipe[0]);
      return EnvMapResult::Err("bootstrap timed out after " +
                              std::to_string(kBootstrapTimeoutSeconds) + "s via " + shell_path);
    }

    const auto remaining_us =
        std::chrono::duration_cast<std::chrono::microseconds>(deadline - now).count();

    fd_set read_fds;
    FD_ZERO(&read_fds);
    int max_fd = -1;
    if (!stdout_done) {
      FD_SET(stdout_pipe[0], &read_fds);
      max_fd = std::max(max_fd, stdout_pipe[0]);
    }
    if (!stderr_done) {
      FD_SET(stderr_pipe[0], &read_fds);
      max_fd = std::max(max_fd, stderr_pipe[0]);
    }

    timeval tv{};
    tv.tv_sec = static_cast<time_t>(remaining_us / 1000000);
    tv.tv_usec = static_cast<suseconds_t>(remaining_us % 1000000);

    const int sel = select(max_fd + 1, &read_fds, nullptr, nullptr, &tv);
    if (sel == 0) {
      kill(child_pid, SIGKILL);
      waitpid(child_pid, nullptr, 0);
      close(stdout_pipe[0]);
      close(stderr_pipe[0]);
      return EnvMapResult::Err("bootstrap timed out after " +
                               std::to_string(kBootstrapTimeoutSeconds) + "s via " + shell_path);
    }
    if (sel < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }

    if (!stdout_done && FD_ISSET(stdout_pipe[0], &read_fds)) {
      const ssize_t n = read(stdout_pipe[0], buffer.data(), buffer.size());
      if (n <= 0) {
        stdout_done = true;
      } else {
        stdout_data.append(buffer.data(), static_cast<std::size_t>(n));
      }
    }

    if (!stderr_done && FD_ISSET(stderr_pipe[0], &read_fds)) {
      const ssize_t n = read(stderr_pipe[0], buffer.data(), buffer.size());
      if (n <= 0) {
        stderr_done = true;
      } else {
        stderr_data.append(buffer.data(), static_cast<std::size_t>(n));
      }
    }
  }

  close(stdout_pipe[0]);
  close(stderr_pipe[0]);

  int status = 0;
  const pid_t waited = waitpid(child_pid, &status, 0);
  static_cast<void>(waited);

  const int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  if (exit_code != 0) {
    std::string error = "failed to bootstrap login-shell environment via " + shell_path +
                        ": exited with code " + std::to_string(exit_code);
    if (!stderr_data.empty()) {
      // Truncate stderr to 256 chars for the error message.
      const std::string truncated =
          stderr_data.size() > 256 ? stderr_data.substr(0, 256) + "..." : stderr_data;
      error += "\nstderr: " + truncated;
    }
    return EnvMapResult::Err(std::move(error));
  }

  auto env = ParseEnvZero(stdout_data);
  if (!stderr_data.empty()) {
    last_warning_ = stderr_data.size() > 512
                        ? stderr_data.substr(0, 512) + "..."
                        : stderr_data;
    std::cerr << "warning: login-shell environment bootstrap via " << shell_path
              << " produced stderr: " << *last_warning_ << '\n';
  }

  return EnvMapResult::Ok(std::move(env));
}

auto BootstrappedEnvCache::Get(const std::string& shell_path) -> EnvMapResult {
  const auto now = std::chrono::steady_clock::now();

  const auto it = cache_.find(shell_path);
  if (it != cache_.end()) {
    const auto age = now - it->second.captured_at;
    if (age < ttl_) {
      return EnvMapResult::Ok(it->second.env);
    }
    // TTL expired -- evict and re-run
    cache_.erase(it);
  }

  auto result = RunBootstrap(shell_path);
  if (!result.has_value()) {
    return EnvMapResult::Err(result.error());
  }

  cache_[shell_path] = Entry{
      .env = result.value(),
      .captured_at = now,
  };
  return result;
}

void BootstrappedEnvCache::Invalidate() {
  cache_.clear();
}

auto BootstrappedEnvCache::TakeLastWarning() -> std::optional<std::string> {
  std::optional<std::string> warning = std::move(last_warning_);
  last_warning_.reset();
  return warning;
}

}  // namespace vibe::session
