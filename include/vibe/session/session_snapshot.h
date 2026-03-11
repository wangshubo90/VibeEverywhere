#ifndef VIBE_SESSION_SESSION_SNAPSHOT_H
#define VIBE_SESSION_SESSION_SNAPSHOT_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

struct SessionSignals {
  std::optional<std::int64_t> last_output_at_unix_ms;
  std::optional<std::int64_t> last_activity_at_unix_ms;
  std::uint64_t current_sequence{0};
  std::size_t recent_file_change_count{0};
  SupervisionState supervision_state{SupervisionState::Quiet};
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
  SessionSignals signals;
  std::vector<std::string> recent_file_changes;
  GitSummary git_summary;
};

}  // namespace vibe::session

#endif
