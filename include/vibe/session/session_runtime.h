#ifndef VIBE_SESSION_SESSION_RUNTIME_H
#define VIBE_SESSION_SESSION_RUNTIME_H

#include <optional>
#include <string_view>

#include "vibe/session/launch_spec.h"
#include "vibe/session/pty_process.h"
#include "vibe/session/terminal_debug_artifact.h"
#include "vibe/session/session_output_buffer.h"
#include "vibe/session/session_record.h"
#include "vibe/session/terminal_multiplexer.h"

namespace vibe::session {

class SessionRuntime {
 public:
  SessionRuntime(SessionRecord record, LaunchSpec launch_spec, IPtyProcess& pty_process,
                 std::size_t output_buffer_capacity_bytes = 8U * 1024U * 1024U);

  [[nodiscard]] auto record() const -> const SessionRecord&;
  [[nodiscard]] auto launch_spec() const -> const LaunchSpec&;
  [[nodiscard]] auto pid() const -> std::optional<ProcessId>;
  [[nodiscard]] auto output_buffer() const -> const SessionOutputBuffer&;
  [[nodiscard]] auto readable_fd() const -> std::optional<int>;
  [[nodiscard]] auto viewport_snapshot(std::string_view view_id) const
      -> std::optional<TerminalViewportSnapshot>;

  [[nodiscard]] auto Start() -> bool;
  [[nodiscard]] auto WriteInput(std::string_view input) -> bool;
  [[nodiscard]] auto ResizeTerminal(TerminalSize terminal_size) -> bool;
  void UpdateViewport(std::string_view view_id, TerminalSize viewport_size);
  void RemoveViewport(std::string_view view_id);
  [[nodiscard]] auto Terminate() -> bool;
  [[nodiscard]] auto TerminateAndMarkExited() -> bool;
  [[nodiscard]] auto Shutdown() -> bool;
  [[nodiscard]] auto MarkAwaitingInput() -> bool;
  [[nodiscard]] auto MarkRunning() -> bool;
  [[nodiscard]] auto HandleExit(bool clean_exit) -> bool;
  void UpdateGitSummary(GitSummary git_summary);
  void UpdateGroupTags(std::vector<std::string> group_tags);
  void UpdateRecentFileChanges(std::vector<std::string> recent_file_changes);
  void PollOnce(int read_timeout_ms);

 private:
  [[nodiscard]] auto IsInteractiveState() const -> bool;

  SessionRecord record_;
  LaunchSpec launch_spec_;
  IPtyProcess& pty_process_;
  std::optional<ProcessId> pid_;
  SessionOutputBuffer output_buffer_;
  TerminalMultiplexer terminal_multiplexer_;
  TerminalDebugRecorder debug_recorder_;
};

}  // namespace vibe::session

#endif
