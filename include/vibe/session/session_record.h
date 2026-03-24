#ifndef VIBE_SESSION_SESSION_RECORD_H
#define VIBE_SESSION_SESSION_RECORD_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "vibe/session/session_lifecycle.h"
#include "vibe/session/session_snapshot.h"
#include "vibe/session/session_types.h"

namespace vibe::session {

class SessionRecord {
 public:
  explicit SessionRecord(SessionMetadata metadata);

  [[nodiscard]] auto metadata() const -> const SessionMetadata&;
  [[nodiscard]] auto lifecycle() const -> const SessionLifecycle&;

  [[nodiscard]] auto TryTransition(SessionStatus next_status) -> bool;
  void SetCurrentSequence(std::uint64_t current_sequence);
  void SetRecentTerminalTail(std::string recent_terminal_tail);
  void SetRecentFileChanges(std::vector<std::string> recent_file_changes);
  void SetGitSummary(GitSummary git_summary);
  void SetGroupTags(std::vector<std::string> group_tags);

  [[nodiscard]] auto snapshot() const -> SessionSnapshot;

 private:
  SessionMetadata metadata_;
  SessionLifecycle lifecycle_;
  std::uint64_t current_sequence_{0};
  std::string recent_terminal_tail_;
  std::vector<std::string> recent_file_changes_;
  GitSummary git_summary_;
};

}  // namespace vibe::session

#endif
