#ifndef VIBE_SESSION_SESSION_RUNTIME_H
#define VIBE_SESSION_SESSION_RUNTIME_H

#include <optional>
#include <string_view>

#include "vibe/session/launch_spec.h"
#include "vibe/session/pty_process.h"
#include "vibe/session/session_record.h"

namespace vibe::session {

class SessionRuntime {
 public:
  SessionRuntime(SessionRecord record, LaunchSpec launch_spec, IPtyProcess& pty_process);

  [[nodiscard]] auto record() const -> const SessionRecord&;
  [[nodiscard]] auto launch_spec() const -> const LaunchSpec&;
  [[nodiscard]] auto pid() const -> std::optional<ProcessId>;

  [[nodiscard]] auto Start() -> bool;
  [[nodiscard]] auto WriteInput(std::string_view input) -> bool;
  [[nodiscard]] auto ResizeTerminal(TerminalSize terminal_size) -> bool;
  [[nodiscard]] auto Terminate() -> bool;
  [[nodiscard]] auto MarkAwaitingInput() -> bool;
  [[nodiscard]] auto MarkRunning() -> bool;
  [[nodiscard]] auto HandleExit(bool clean_exit) -> bool;

 private:
  [[nodiscard]] auto IsInteractiveState() const -> bool;

  SessionRecord record_;
  LaunchSpec launch_spec_;
  IPtyProcess& pty_process_;
  std::optional<ProcessId> pid_;
};

}  // namespace vibe::session

#endif
