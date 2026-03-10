#include "vibe/session/session_runtime.h"

#include <utility>

namespace vibe::session {

SessionRuntime::SessionRuntime(SessionRecord record, LaunchSpec launch_spec, IPtyProcess& pty_process)
    : record_(std::move(record)),
      launch_spec_(std::move(launch_spec)),
      pty_process_(pty_process) {}

auto SessionRuntime::record() const -> const SessionRecord& { return record_; }

auto SessionRuntime::launch_spec() const -> const LaunchSpec& { return launch_spec_; }

auto SessionRuntime::pid() const -> std::optional<ProcessId> { return pid_; }

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

auto SessionRuntime::IsInteractiveState() const -> bool {
  const SessionStatus state = record_.metadata().status;
  return state == SessionStatus::Running || state == SessionStatus::AwaitingInput;
}

}  // namespace vibe::session
