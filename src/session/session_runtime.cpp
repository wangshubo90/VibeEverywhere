#include "vibe/session/session_runtime.h"

#include <utility>

namespace vibe::session {

SessionRuntime::SessionRuntime(SessionRecord record, LaunchSpec launch_spec, IPtyProcess& pty_process,
                               const std::size_t output_buffer_capacity_bytes)
    : record_(std::move(record)),
      launch_spec_(std::move(launch_spec)),
      pty_process_(pty_process),
      output_buffer_(output_buffer_capacity_bytes) {}

auto SessionRuntime::record() const -> const SessionRecord& { return record_; }

auto SessionRuntime::launch_spec() const -> const LaunchSpec& { return launch_spec_; }

auto SessionRuntime::pid() const -> std::optional<ProcessId> { return pid_; }

auto SessionRuntime::output_buffer() const -> const SessionOutputBuffer& { return output_buffer_; }

auto SessionRuntime::readable_fd() const -> std::optional<int> { return pty_process_.ReadableFd(); }

auto SessionRuntime::Start() -> bool {
  if (!record_.TryTransition(SessionStatus::Starting)) {
    return false;
  }

  const StartResult start_result = pty_process_.Start(launch_spec_);
  if (!start_result.started) {
    const bool transitioned_to_error = record_.TryTransition(SessionStatus::Error);
    static_cast<void>(transitioned_to_error);
    return false;
  }

  pid_ = start_result.pid;
  return record_.TryTransition(SessionStatus::Running);
}

auto SessionRuntime::WriteInput(const std::string_view input) -> bool {
  if (!IsInteractiveState()) {
    return false;
  }

  return pty_process_.Write(input);
}

auto SessionRuntime::ResizeTerminal(const TerminalSize terminal_size) -> bool {
  if (!IsInteractiveState()) {
    return false;
  }

  launch_spec_.terminal_size = terminal_size;
  return pty_process_.Resize(terminal_size);
}

auto SessionRuntime::Terminate() -> bool {
  if (!pid_.has_value()) {
    return false;
  }

  return pty_process_.Terminate();
}

auto SessionRuntime::TerminateAndMarkExited() -> bool {
  if (!pid_.has_value()) {
    return false;
  }

  const bool terminated = pty_process_.Terminate();
  if (!terminated) {
    return false;
  }

  pid_.reset();
  return record_.TryTransition(SessionStatus::Exited);
}

auto SessionRuntime::Shutdown() -> bool {
  const SessionStatus status = record_.metadata().status;
  if (status == SessionStatus::Exited || status == SessionStatus::Error) {
    pid_.reset();
    return true;
  }

  if (pid_.has_value()) {
    const bool terminated = pty_process_.Terminate();
    if (!terminated) {
      return false;
    }
    pid_.reset();
  }

  if (status == SessionStatus::Running || status == SessionStatus::AwaitingInput) {
    return record_.TryTransition(SessionStatus::Exited);
  }

  if (status == SessionStatus::Starting) {
    return record_.TryTransition(SessionStatus::Error);
  }

  return status == SessionStatus::Created;
}

auto SessionRuntime::MarkAwaitingInput() -> bool {
  return record_.TryTransition(SessionStatus::AwaitingInput);
}

auto SessionRuntime::MarkRunning() -> bool { return record_.TryTransition(SessionStatus::Running); }

auto SessionRuntime::HandleExit(const bool clean_exit) -> bool {
  const bool transitioned =
      record_.TryTransition(clean_exit ? SessionStatus::Exited : SessionStatus::Error);

  if (!transitioned) {
    return false;
  }

  pid_.reset();
  return true;
}

void SessionRuntime::UpdateGitSummary(GitSummary git_summary) {
  record_.SetGitSummary(std::move(git_summary));
}

void SessionRuntime::UpdateGroupTags(std::vector<std::string> group_tags) {
  record_.SetGroupTags(std::move(group_tags));
}

void SessionRuntime::UpdateRecentFileChanges(std::vector<std::string> recent_file_changes) {
  record_.SetRecentFileChanges(std::move(recent_file_changes));
}

void SessionRuntime::PollOnce(const int read_timeout_ms) {
  if (!pid_.has_value()) {
    return;
  }

  int timeout_ms_remaining = read_timeout_ms;
  while (true) {
    const ReadResult read_result = pty_process_.Read(timeout_ms_remaining);
    timeout_ms_remaining = 0;

    if (!read_result.data.empty()) {
      output_buffer_.Append(read_result.data);
      record_.SetCurrentSequence(output_buffer_.next_sequence() - 1);
      record_.SetRecentTerminalTail(output_buffer_.Tail(64U * 1024U).data);
    }

    if (read_result.closed) {
      const std::optional<int> exit_code = pty_process_.PollExit();
      if (exit_code.has_value()) {
        const bool handled_exit = HandleExit(*exit_code == 0);
        static_cast<void>(handled_exit);
      }
      return;
    }

    if (read_result.data.empty()) {
      break;
    }
  }

  const std::optional<int> exit_code = pty_process_.PollExit();
  if (exit_code.has_value()) {
    const bool handled_exit = HandleExit(*exit_code == 0);
    static_cast<void>(handled_exit);
  }
}

auto SessionRuntime::IsInteractiveState() const -> bool {
  const SessionStatus state = record_.metadata().status;
  return state == SessionStatus::Running || state == SessionStatus::AwaitingInput;
}

}  // namespace vibe::session
