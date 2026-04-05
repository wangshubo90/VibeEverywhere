#include "vibe/session/session_record.h"

#include <utility>

namespace vibe::session {

SessionRecord::SessionRecord(SessionMetadata metadata) : metadata_(std::move(metadata)) {}

auto SessionRecord::metadata() const -> const SessionMetadata& { return metadata_; }

auto SessionRecord::lifecycle() const -> const SessionLifecycle& { return lifecycle_; }

auto SessionRecord::TryTransition(SessionStatus next_status) -> bool {
  if (!lifecycle_.TryTransition(next_status)) {
    return false;
  }

  metadata_.status = lifecycle_.state();
  return true;
}

void SessionRecord::SetCurrentSequence(const std::uint64_t current_sequence) {
  current_sequence_ = current_sequence;
}

void SessionRecord::SetRecentTerminalTail(std::string recent_terminal_tail) {
  recent_terminal_tail_ = std::move(recent_terminal_tail);
}

void SessionRecord::SetTerminalScreen(TerminalScreenSnapshot terminal_screen) {
  terminal_screen_ = std::move(terminal_screen);
}

void SessionRecord::SetRecentFileChanges(std::vector<std::string> recent_file_changes) {
  recent_file_changes_ = std::move(recent_file_changes);
}

void SessionRecord::SetGitSummary(GitSummary git_summary) { git_summary_ = std::move(git_summary); }

void SessionRecord::SetGroupTags(std::vector<std::string> group_tags) {
  metadata_.group_tags = std::move(group_tags);
}

auto SessionRecord::snapshot() const -> SessionSnapshot {
  return SessionSnapshot{
      .metadata = metadata_,
      .current_sequence = current_sequence_,
      .recent_terminal_tail = recent_terminal_tail_,
      .terminal_screen = terminal_screen_,
      .signals =
          SessionSignals{
              .last_output_at_unix_ms = std::nullopt,
              .last_activity_at_unix_ms = std::nullopt,
              .last_file_change_at_unix_ms = std::nullopt,
              .last_git_change_at_unix_ms = std::nullopt,
              .last_controller_change_at_unix_ms = std::nullopt,
              .attention_since_unix_ms = std::nullopt,
              .pty_columns = std::nullopt,
              .pty_rows = std::nullopt,
              .current_sequence = current_sequence_,
              .recent_file_change_count = recent_file_changes_.size(),
              .attention_state = AttentionState::None,
              .attention_reason = AttentionReason::None,
              .git_dirty = !git_summary_.modified_files.empty() || !git_summary_.staged_files.empty() ||
                           !git_summary_.untracked_files.empty(),
              .git_branch = git_summary_.branch,
          },
      .recent_file_changes = recent_file_changes_,
      .git_summary = git_summary_,
  };
}

}  // namespace vibe::session
