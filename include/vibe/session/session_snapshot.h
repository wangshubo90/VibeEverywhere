#ifndef VIBE_SESSION_SESSION_SNAPSHOT_H
#define VIBE_SESSION_SESSION_SNAPSHOT_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "vibe/session/session_types.h"

namespace vibe::session {

struct SessionSignals {
  std::optional<std::int64_t> last_output_at_unix_ms;
  std::optional<std::int64_t> last_activity_at_unix_ms;
  std::uint64_t current_sequence{0};
  std::size_t recent_file_change_count{0};
  bool git_dirty{false};
  std::string git_branch;
};

struct GitSummary {
  std::string branch;
  std::vector<std::string> modified_files;
  std::vector<std::string> staged_files;
  std::vector<std::string> untracked_files;
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
