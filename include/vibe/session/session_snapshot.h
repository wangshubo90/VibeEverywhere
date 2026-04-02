#ifndef VIBE_SESSION_SESSION_SNAPSHOT_H
#define VIBE_SESSION_SESSION_SNAPSHOT_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "vibe/session/terminal_multiplexer.h"
#include "vibe/session/session_types.h"

namespace vibe::session {

enum class SupervisionState {
  Active,
  Quiet,
  Stopped,
};

[[nodiscard]] constexpr auto ToString(const SupervisionState state) -> std::string_view {
  switch (state) {
    case SupervisionState::Active:
      return "active";
    case SupervisionState::Quiet:
      return "quiet";
    case SupervisionState::Stopped:
      return "stopped";
  }

  return "quiet";
}

enum class AttentionState {
  None,
  Info,
  ActionRequired,
  Intervention,
};

[[nodiscard]] constexpr auto ToString(const AttentionState state) -> std::string_view {
  switch (state) {
    case AttentionState::None:
      return "none";
    case AttentionState::Info:
      return "info";
    case AttentionState::ActionRequired:
      return "action_required";
    case AttentionState::Intervention:
      return "intervention";
  }

  return "none";
}

enum class AttentionReason {
  None,
  AwaitingInput,
  SessionError,
  WorkspaceChanged,
  GitStateChanged,
  ControllerChanged,
  SessionExitedCleanly,
};

[[nodiscard]] constexpr auto ToString(const AttentionReason reason) -> std::string_view {
  switch (reason) {
    case AttentionReason::None:
      return "none";
    case AttentionReason::AwaitingInput:
      return "awaiting_input";
    case AttentionReason::SessionError:
      return "session_error";
    case AttentionReason::WorkspaceChanged:
      return "workspace_changed";
    case AttentionReason::GitStateChanged:
      return "git_state_changed";
    case AttentionReason::ControllerChanged:
      return "controller_changed";
    case AttentionReason::SessionExitedCleanly:
      return "session_exited_cleanly";
  }

  return "none";
}

struct SessionSignals {
  std::optional<std::int64_t> last_output_at_unix_ms;
  std::optional<std::int64_t> last_activity_at_unix_ms;
  std::optional<std::int64_t> last_file_change_at_unix_ms;
  std::optional<std::int64_t> last_git_change_at_unix_ms;
  std::optional<std::int64_t> last_controller_change_at_unix_ms;
  std::optional<std::int64_t> attention_since_unix_ms;
  std::optional<std::uint16_t> pty_columns;
  std::optional<std::uint16_t> pty_rows;
  std::uint64_t current_sequence{0};
  std::size_t recent_file_change_count{0};
  SupervisionState supervision_state{SupervisionState::Quiet};
  AttentionState attention_state{AttentionState::None};
  AttentionReason attention_reason{AttentionReason::None};
  bool git_dirty{false};
  std::string git_branch;
  std::size_t git_modified_count{0};
  std::size_t git_staged_count{0};
  std::size_t git_untracked_count{0};
};

struct GitSummary {
  std::string branch;
  std::size_t modified_count{0};
  std::size_t staged_count{0};
  std::size_t untracked_count{0};
  std::vector<std::string> modified_files;
  std::vector<std::string> staged_files;
  std::vector<std::string> untracked_files;

  [[nodiscard]] auto operator==(const GitSummary& other) const -> bool = default;
};

struct SessionSnapshot {
  SessionMetadata metadata;
  std::uint64_t current_sequence{0};
  std::string recent_terminal_tail;
  std::optional<TerminalScreenSnapshot> terminal_screen;
  SessionSignals signals;
  std::vector<std::string> recent_file_changes;
  GitSummary git_summary;
};

}  // namespace vibe::session

#endif
