#include <gtest/gtest.h>

#include "vibe/session/session_record.h"

namespace vibe::session {
namespace {

auto MakeMetadata() -> SessionMetadata {
  const auto session_id = SessionId::TryCreate("record_001");
  EXPECT_TRUE(session_id.has_value());

  return SessionMetadata{
      .id = *session_id,
      .provider = ProviderType::Codex,
      .workspace_root = "/tmp/project",
      .title = "session-record",
      .status = SessionStatus::Created,
      .conversation_id = std::nullopt,
  };
}

TEST(SessionRecordTest, StartsWithMetadataAndCreatedLifecycle) {
  const SessionRecord record(MakeMetadata());

  EXPECT_EQ(record.metadata().id.value(), "record_001");
  EXPECT_EQ(record.metadata().provider, ProviderType::Codex);
  EXPECT_EQ(record.metadata().status, SessionStatus::Created);
  EXPECT_EQ(record.lifecycle().state(), SessionStatus::Created);
}

TEST(SessionRecordTest, TransitionUpdatesMetadataStatus) {
  SessionRecord record(MakeMetadata());

  ASSERT_TRUE(record.TryTransition(SessionStatus::Starting));
  ASSERT_TRUE(record.TryTransition(SessionStatus::Running));

  EXPECT_EQ(record.metadata().status, SessionStatus::Running);
  EXPECT_EQ(record.lifecycle().state(), SessionStatus::Running);
}

TEST(SessionRecordTest, InvalidTransitionLeavesMetadataUnchanged) {
  SessionRecord record(MakeMetadata());

  EXPECT_FALSE(record.TryTransition(SessionStatus::Running));
  EXPECT_EQ(record.metadata().status, SessionStatus::Created);
  EXPECT_EQ(record.lifecycle().state(), SessionStatus::Created);
}

TEST(SessionRecordTest, SnapshotReflectsAccumulatedSessionState) {
  SessionRecord record(MakeMetadata());

  ASSERT_TRUE(record.TryTransition(SessionStatus::Starting));
  ASSERT_TRUE(record.TryTransition(SessionStatus::Running));
  record.SetCurrentSequence(128);
  record.SetRecentTerminalTail("git status\n");
  record.SetRecentFileChanges({"src/main.cpp", "README.md"});
  record.SetGitSummary(GitSummary{
      .branch = "feature/session-record",
      .modified_count = 1,
      .staged_count = 1,
      .untracked_count = 1,
      .modified_files = {"src/main.cpp"},
      .staged_files = {"README.md"},
      .untracked_files = {"notes.md"},
  });

  const SessionSnapshot snapshot = record.snapshot();
  EXPECT_EQ(snapshot.metadata.status, SessionStatus::Running);
  EXPECT_EQ(snapshot.current_sequence, 128U);
  EXPECT_EQ(snapshot.recent_terminal_tail, "git status\n");
  EXPECT_EQ(snapshot.recent_file_changes, (std::vector<std::string>{"src/main.cpp", "README.md"}));
  EXPECT_EQ(snapshot.git_summary.branch, "feature/session-record");
  EXPECT_EQ(snapshot.git_summary.modified_count, 1U);
  EXPECT_EQ(snapshot.git_summary.staged_count, 1U);
  EXPECT_EQ(snapshot.git_summary.untracked_count, 1U);
  EXPECT_EQ(snapshot.git_summary.modified_files, (std::vector<std::string>{"src/main.cpp"}));
  EXPECT_EQ(snapshot.git_summary.staged_files, (std::vector<std::string>{"README.md"}));
  EXPECT_EQ(snapshot.git_summary.untracked_files, (std::vector<std::string>{"notes.md"}));
}

}  // namespace
}  // namespace vibe::session
