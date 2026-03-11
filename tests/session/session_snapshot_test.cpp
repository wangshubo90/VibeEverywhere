#include <gtest/gtest.h>

#include "vibe/session/session_snapshot.h"

namespace vibe::session {
namespace {

TEST(SessionSnapshotTest, CarriesLightweightRecoveryState) {
  const auto session_id = SessionId::TryCreate("snapshot_001");
  ASSERT_TRUE(session_id.has_value());

  const SessionSnapshot snapshot{
      .metadata =
          SessionMetadata{
              .id = *session_id,
              .provider = ProviderType::Codex,
              .workspace_root = "/tmp/project",
              .title = "recoverable-session",
              .status = SessionStatus::Running,
          },
      .current_sequence = 42,
      .recent_terminal_tail = "Running tests...\nDone.\n",
      .signals =
          SessionSignals{
              .last_output_at_unix_ms = 100,
              .last_activity_at_unix_ms = 110,
              .current_sequence = 42,
              .recent_file_change_count = 2,
              .git_dirty = true,
              .git_branch = "main",
              .git_modified_count = 1,
              .git_staged_count = 1,
              .git_untracked_count = 1,
          },
      .recent_file_changes = {"src/main.cpp", "tests/session_test.cpp"},
      .git_summary =
          GitSummary{
              .branch = "main",
              .modified_count = 1,
              .staged_count = 1,
              .untracked_count = 1,
              .modified_files = {"src/main.cpp"},
              .staged_files = {"CMakeLists.txt"},
              .untracked_files = {"notes.txt"},
          },
  };

  EXPECT_EQ(snapshot.metadata.id.value(), "snapshot_001");
  EXPECT_EQ(snapshot.metadata.provider, ProviderType::Codex);
  EXPECT_EQ(snapshot.metadata.status, SessionStatus::Running);
  EXPECT_EQ(snapshot.current_sequence, 42U);
  EXPECT_EQ(snapshot.recent_terminal_tail, "Running tests...\nDone.\n");
  EXPECT_EQ(snapshot.signals.last_output_at_unix_ms, std::optional<std::int64_t>{100});
  EXPECT_EQ(snapshot.signals.last_activity_at_unix_ms, std::optional<std::int64_t>{110});
  EXPECT_EQ(snapshot.signals.current_sequence, 42U);
  EXPECT_EQ(snapshot.signals.recent_file_change_count, 2U);
  EXPECT_EQ(snapshot.signals.supervision_state, SupervisionState::Quiet);
  EXPECT_TRUE(snapshot.signals.git_dirty);
  EXPECT_EQ(snapshot.signals.git_branch, "main");
  EXPECT_EQ(snapshot.signals.git_modified_count, 1U);
  EXPECT_EQ(snapshot.signals.git_staged_count, 1U);
  EXPECT_EQ(snapshot.signals.git_untracked_count, 1U);
  EXPECT_EQ(snapshot.recent_file_changes,
            (std::vector<std::string>{"src/main.cpp", "tests/session_test.cpp"}));
  EXPECT_EQ(snapshot.git_summary.branch, "main");
  EXPECT_EQ(snapshot.git_summary.modified_count, 1U);
  EXPECT_EQ(snapshot.git_summary.staged_count, 1U);
  EXPECT_EQ(snapshot.git_summary.untracked_count, 1U);
  EXPECT_EQ(snapshot.git_summary.modified_files, (std::vector<std::string>{"src/main.cpp"}));
  EXPECT_EQ(snapshot.git_summary.staged_files, (std::vector<std::string>{"CMakeLists.txt"}));
  EXPECT_EQ(snapshot.git_summary.untracked_files, (std::vector<std::string>{"notes.txt"}));
}

TEST(SessionSnapshotTest, DefaultsToEmptyOptionalCollections) {
  const auto session_id = SessionId::TryCreate("snapshot_002");
  ASSERT_TRUE(session_id.has_value());

  const SessionSnapshot snapshot{
      .metadata =
          SessionMetadata{
              .id = *session_id,
              .provider = ProviderType::Claude,
              .workspace_root = "/tmp/project",
              .title = "idle-session",
              .status = SessionStatus::Created,
          },
      .current_sequence = 0,
      .recent_terminal_tail = "",
      .signals = {},
      .recent_file_changes = {},
      .git_summary = {},
  };

  EXPECT_EQ(snapshot.current_sequence, 0U);
  EXPECT_TRUE(snapshot.recent_terminal_tail.empty());
  EXPECT_FALSE(snapshot.signals.last_output_at_unix_ms.has_value());
  EXPECT_FALSE(snapshot.signals.last_activity_at_unix_ms.has_value());
  EXPECT_EQ(snapshot.signals.current_sequence, 0U);
  EXPECT_EQ(snapshot.signals.recent_file_change_count, 0U);
  EXPECT_EQ(snapshot.signals.supervision_state, SupervisionState::Quiet);
  EXPECT_FALSE(snapshot.signals.git_dirty);
  EXPECT_TRUE(snapshot.signals.git_branch.empty());
  EXPECT_EQ(snapshot.signals.git_modified_count, 0U);
  EXPECT_EQ(snapshot.signals.git_staged_count, 0U);
  EXPECT_EQ(snapshot.signals.git_untracked_count, 0U);
  EXPECT_TRUE(snapshot.recent_file_changes.empty());
  EXPECT_TRUE(snapshot.git_summary.branch.empty());
  EXPECT_EQ(snapshot.git_summary.modified_count, 0U);
  EXPECT_EQ(snapshot.git_summary.staged_count, 0U);
  EXPECT_EQ(snapshot.git_summary.untracked_count, 0U);
  EXPECT_TRUE(snapshot.git_summary.modified_files.empty());
  EXPECT_TRUE(snapshot.git_summary.staged_files.empty());
  EXPECT_TRUE(snapshot.git_summary.untracked_files.empty());
}

}  // namespace
}  // namespace vibe::session
