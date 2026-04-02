#include <gtest/gtest.h>

#include "vibe/net/json.h"

namespace vibe::net {
namespace {

TEST(HttpJsonTest, EscapesQuotedAndControlCharacters) {
  EXPECT_EQ(JsonEscape("a\"b\\c\n"), "a\\\"b\\\\c\\n");
}

TEST(HttpJsonTest, SerializesHostInfo) {
  const std::string json = ToJsonHostInfo();
  EXPECT_NE(json.find("\"hostId\":\"\""), std::string::npos);
  EXPECT_NE(json.find("\"displayName\":\"Sentrits Host\""), std::string::npos);
  EXPECT_NE(json.find("\"adminHost\":\"127.0.0.1\""), std::string::npos);
  EXPECT_NE(json.find("\"adminPort\":18085"), std::string::npos);
  EXPECT_NE(json.find("\"remoteHost\":\"0.0.0.0\""), std::string::npos);
  EXPECT_NE(json.find("\"remotePort\":18086"), std::string::npos);
  EXPECT_NE(json.find("\"providerCommands\""), std::string::npos);
  EXPECT_NE(json.find("\"capabilities\":[\"sessions\",\"rest\",\"websocket\"]"),
            std::string::npos);
}

TEST(HttpJsonTest, SerializesDiscoveryInfo) {
  const std::string json = ToJson(DiscoveryInfo{
      .host_id = "host_1",
      .display_name = "Dev Host",
      .remote_host = "192.168.1.10",
      .remote_port = 19086,
      .protocol_version = "1",
      .tls = true,
  });

  EXPECT_NE(json.find("\"hostId\":\"host_1\""), std::string::npos);
  EXPECT_NE(json.find("\"displayName\":\"Dev Host\""), std::string::npos);
  EXPECT_NE(json.find("\"remoteHost\":\"192.168.1.10\""), std::string::npos);
  EXPECT_NE(json.find("\"remotePort\":19086"), std::string::npos);
  EXPECT_NE(json.find("\"protocolVersion\":\"1\""), std::string::npos);
  EXPECT_NE(json.find("\"tls\":true"), std::string::npos);
}

TEST(HttpJsonTest, SerializesSessionSummaryControllerFields) {
  const auto id = vibe::session::SessionId::TryCreate("s_host");
  ASSERT_TRUE(id.has_value());

  const std::string json = ToJson(vibe::service::SessionSummary{
      .id = *id,
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = ".",
      .title = "demo",
      .status = vibe::session::SessionStatus::Running,
      .conversation_id = "conv_hash_1",
      .group_tags = {"frontend", "mvp"},
      .controller_client_id = std::nullopt,
      .controller_kind = vibe::session::ControllerKind::Host,
      .is_recovered = false,
      .is_active = true,
      .supervision_state = vibe::session::SupervisionState::Active,
      .attention_state = vibe::session::AttentionState::Info,
      .attention_reason = vibe::session::AttentionReason::WorkspaceChanged,
      .created_at_unix_ms = 100,
      .last_status_at_unix_ms = 200,
      .last_output_at_unix_ms = 210,
      .last_activity_at_unix_ms = 220,
      .last_file_change_at_unix_ms = 220,
      .last_git_change_at_unix_ms = std::nullopt,
      .last_controller_change_at_unix_ms = std::nullopt,
      .attention_since_unix_ms = 220,
      .pty_columns = std::nullopt,
      .pty_rows = std::nullopt,
      .current_sequence = 12,
      .attached_client_count = 1,
      .recent_file_change_count = 0,
      .git_dirty = false,
      .git_branch = "",
      .git_modified_count = 0,
      .git_staged_count = 0,
      .git_untracked_count = 0,
  });

  EXPECT_NE(json.find("\"controllerKind\":\"host\""), std::string::npos);
  EXPECT_NE(json.find("\"conversationId\":\"conv_hash_1\""), std::string::npos);
  EXPECT_NE(json.find("\"groupTags\":[\"frontend\",\"mvp\"]"), std::string::npos);
  EXPECT_NE(json.find("\"archivedRecord\":false"), std::string::npos);
  EXPECT_NE(json.find("\"inventoryState\":\"live\""), std::string::npos);
  EXPECT_NE(json.find("\"attentionState\":\"info\""), std::string::npos);
  EXPECT_NE(json.find("\"attentionReason\":\"workspace_changed\""), std::string::npos);
  EXPECT_NE(json.find("\"activityState\":\"active\""), std::string::npos);
  EXPECT_NE(json.find("\"supervisionState\":\"active\""), std::string::npos);
  EXPECT_NE(json.find("\"createdAtUnixMs\":100"), std::string::npos);
  EXPECT_NE(json.find("\"lastStatusAtUnixMs\":200"), std::string::npos);
  EXPECT_NE(json.find("\"lastOutputAtUnixMs\":210"), std::string::npos);
  EXPECT_NE(json.find("\"lastActivityAtUnixMs\":220"), std::string::npos);
  EXPECT_NE(json.find("\"currentSequence\":12"), std::string::npos);
  EXPECT_NE(json.find("\"attachedClientCount\":1"), std::string::npos);
}

TEST(HttpJsonTest, SerializesOutputSlice) {
  const std::string json = ToJson(vibe::session::OutputSlice{
      .seq_start = 3,
      .seq_end = 4,
      .data = "hello\n",
  });

  EXPECT_NE(json.find("\"seqStart\":3"), std::string::npos);
  EXPECT_NE(json.find("\"seqEnd\":4"), std::string::npos);
  EXPECT_NE(json.find("\"dataEncoding\":\"base64\""), std::string::npos);
  EXPECT_NE(json.find("\"dataBase64\":\"aGVsbG8K\""), std::string::npos);
}

TEST(HttpJsonTest, SerializesSessionFileReadResult) {
  const std::string json = ToJson(vibe::service::SessionFileReadResult{
      .status = vibe::service::FileReadStatus::Ok,
      .workspace_path = "src/main.cpp",
      .content = "hello\n",
      .size_bytes = 12,
      .truncated = true,
  });

  EXPECT_NE(json.find("\"workspacePath\":\"src/main.cpp\""), std::string::npos);
  EXPECT_NE(json.find("\"contentEncoding\":\"base64\""), std::string::npos);
  EXPECT_NE(json.find("\"contentBase64\":\"aGVsbG8K\""), std::string::npos);
  EXPECT_NE(json.find("\"sizeBytes\":12"), std::string::npos);
  EXPECT_NE(json.find("\"truncated\":true"), std::string::npos);
}

TEST(HttpJsonTest, SerializesSnapshotSignals) {
  const auto session_id = vibe::session::SessionId::TryCreate("snapshot_001");
  ASSERT_TRUE(session_id.has_value());

  const std::string json = ToJson(vibe::session::SessionSnapshot{
      .metadata =
          vibe::session::SessionMetadata{
              .id = *session_id,
              .provider = vibe::session::ProviderType::Codex,
              .workspace_root = "/tmp/project",
              .title = "recoverable-session",
              .status = vibe::session::SessionStatus::Running,
              .conversation_id = std::nullopt,
              .group_tags = {"frontend", "mvp"},
          },
      .current_sequence = 42,
      .recent_terminal_tail = "tail",
      .signals =
          vibe::session::SessionSignals{
              .last_output_at_unix_ms = 100,
              .last_activity_at_unix_ms = 110,
              .last_file_change_at_unix_ms = 110,
              .last_git_change_at_unix_ms = 111,
              .last_controller_change_at_unix_ms = 112,
              .attention_since_unix_ms = 110,
              .pty_columns = std::nullopt,
              .pty_rows = std::nullopt,
              .current_sequence = 42,
              .recent_file_change_count = 2,
              .supervision_state = vibe::session::SupervisionState::Active,
              .attention_state = vibe::session::AttentionState::Info,
              .attention_reason = vibe::session::AttentionReason::WorkspaceChanged,
              .git_dirty = true,
              .git_branch = "main",
              .git_modified_count = 1,
              .git_staged_count = 1,
              .git_untracked_count = 1,
          },
      .recent_file_changes = {"src/main.cpp", "tests/session_test.cpp"},
      .git_summary =
          vibe::session::GitSummary{
              .branch = "main",
              .modified_count = 1,
              .staged_count = 1,
              .untracked_count = 1,
              .modified_files = {"src/main.cpp"},
              .staged_files = {"CMakeLists.txt"},
              .untracked_files = {"notes.txt"},
          },
  });

  EXPECT_NE(json.find("\"signals\""), std::string::npos);
  EXPECT_NE(json.find("\"groupTags\":[\"frontend\",\"mvp\"]"), std::string::npos);
  EXPECT_NE(json.find("\"lastOutputAtUnixMs\":100"), std::string::npos);
  EXPECT_NE(json.find("\"lastActivityAtUnixMs\":110"), std::string::npos);
  EXPECT_NE(json.find("\"currentSequence\":42"), std::string::npos);
  EXPECT_NE(json.find("\"recentFileChangeCount\":2"), std::string::npos);
  EXPECT_NE(json.find("\"supervisionState\":\"active\""), std::string::npos);
  EXPECT_NE(json.find("\"attentionState\":\"info\""), std::string::npos);
  EXPECT_NE(json.find("\"attentionReason\":\"workspace_changed\""), std::string::npos);
  EXPECT_NE(json.find("\"gitDirty\":true"), std::string::npos);
  EXPECT_NE(json.find("\"gitBranch\":\"main\""), std::string::npos);
  EXPECT_NE(json.find("\"gitModifiedCount\":1"), std::string::npos);
  EXPECT_NE(json.find("\"gitStagedCount\":1"), std::string::npos);
  EXPECT_NE(json.find("\"gitUntrackedCount\":1"), std::string::npos);
  EXPECT_NE(json.find("\"modifiedCount\":1"), std::string::npos);
  EXPECT_NE(json.find("\"stagedCount\":1"), std::string::npos);
  EXPECT_NE(json.find("\"untrackedCount\":1"), std::string::npos);
}

TEST(HttpJsonTest, SerializesTerminalOutputEvent) {
  const std::string json = ToJson(TerminalOutputEvent{
      .session_id = "s_7",
      .slice =
          vibe::session::OutputSlice{
              .seq_start = 9,
              .seq_end = 10,
              .data = "tail",
          },
  });

  EXPECT_NE(json.find("\"type\":\"terminal.output\""), std::string::npos);
  EXPECT_NE(json.find("\"sessionId\":\"s_7\""), std::string::npos);
  EXPECT_NE(json.find("\"seqStart\":9"), std::string::npos);
  EXPECT_NE(json.find("\"seqEnd\":10"), std::string::npos);
  EXPECT_NE(json.find("\"dataEncoding\":\"base64\""), std::string::npos);
  EXPECT_NE(json.find("\"dataBase64\":\"dGFpbA==\""), std::string::npos);
}

TEST(HttpJsonTest, SerializesSessionUpdatedEvent) {
  const auto id = vibe::session::SessionId::TryCreate("s_9");
  ASSERT_TRUE(id.has_value());

  const std::string json = ToJson(SessionUpdatedEvent{
      .summary =
          vibe::service::SessionSummary{
              .id = *id,
              .provider = vibe::session::ProviderType::Codex,
              .workspace_root = "/tmp/project",
              .title = "demo",
              .status = vibe::session::SessionStatus::Running,
              .conversation_id = std::nullopt,
              .group_tags = {"frontend"},
              .controller_client_id = "client-1",
              .controller_kind = vibe::session::ControllerKind::Remote,
              .is_recovered = false,
              .is_active = true,
              .supervision_state = vibe::session::SupervisionState::Active,
              .attention_state = vibe::session::AttentionState::Info,
              .attention_reason = vibe::session::AttentionReason::ControllerChanged,
              .created_at_unix_ms = 100,
              .last_status_at_unix_ms = 200,
              .last_output_at_unix_ms = 210,
              .last_activity_at_unix_ms = 220,
              .last_file_change_at_unix_ms = std::nullopt,
              .last_git_change_at_unix_ms = std::nullopt,
              .last_controller_change_at_unix_ms = 220,
              .attention_since_unix_ms = 220,
              .pty_columns = std::nullopt,
              .pty_rows = std::nullopt,
              .current_sequence = 12,
              .attached_client_count = 1,
              .recent_file_change_count = 0,
              .git_dirty = false,
              .git_branch = "",
              .git_modified_count = 0,
              .git_staged_count = 0,
              .git_untracked_count = 0,
          },
  });

  EXPECT_NE(json.find("\"type\":\"session.updated\""), std::string::npos);
  EXPECT_NE(json.find("\"sessionId\":\"s_9\""), std::string::npos);
  EXPECT_NE(json.find("\"status\":\"Running\""), std::string::npos);
  EXPECT_NE(json.find("\"groupTags\":[\"frontend\"]"), std::string::npos);
  EXPECT_NE(json.find("\"controllerClientId\":\"client-1\""), std::string::npos);
  EXPECT_NE(json.find("\"controllerKind\":\"remote\""), std::string::npos);
  EXPECT_NE(json.find("\"archivedRecord\":false"), std::string::npos);
  EXPECT_NE(json.find("\"inventoryState\":\"live\""), std::string::npos);
  EXPECT_NE(json.find("\"attentionState\":\"info\""), std::string::npos);
  EXPECT_NE(json.find("\"attentionReason\":\"controller_changed\""), std::string::npos);
  EXPECT_NE(json.find("\"activityState\":\"active\""), std::string::npos);
  EXPECT_NE(json.find("\"supervisionState\":\"active\""), std::string::npos);
  EXPECT_NE(json.find("\"createdAtUnixMs\":100"), std::string::npos);
  EXPECT_NE(json.find("\"lastStatusAtUnixMs\":200"), std::string::npos);
  EXPECT_NE(json.find("\"lastOutputAtUnixMs\":210"), std::string::npos);
  EXPECT_NE(json.find("\"lastActivityAtUnixMs\":220"), std::string::npos);
  EXPECT_NE(json.find("\"currentSequence\":12"), std::string::npos);
  EXPECT_NE(json.find("\"attachedClientCount\":1"), std::string::npos);
}

TEST(HttpJsonTest, SerializesSessionExitedEvent) {
  const std::string json = ToJson(SessionExitedEvent{
      .session_id = "s_9",
      .status = vibe::session::SessionStatus::Exited,
  });

  EXPECT_NE(json.find("\"type\":\"session.exited\""), std::string::npos);
  EXPECT_NE(json.find("\"sessionId\":\"s_9\""), std::string::npos);
  EXPECT_NE(json.find("\"status\":\"Exited\""), std::string::npos);
}

TEST(HttpJsonTest, SerializesSessionActivityEvent) {
  const auto id = vibe::session::SessionId::TryCreate("s_9");
  ASSERT_TRUE(id.has_value());

  const std::string json = ToJson(SessionActivityEvent{
      .summary =
          vibe::service::SessionSummary{
              .id = *id,
              .provider = vibe::session::ProviderType::Codex,
              .workspace_root = "/tmp/project",
              .title = "demo",
              .status = vibe::session::SessionStatus::Running,
              .conversation_id = std::nullopt,
              .group_tags = {"frontend"},
              .controller_client_id = "client-1",
              .controller_kind = vibe::session::ControllerKind::Remote,
              .is_recovered = false,
              .is_active = true,
              .supervision_state = vibe::session::SupervisionState::Active,
              .attention_state = vibe::session::AttentionState::Info,
              .attention_reason = vibe::session::AttentionReason::GitStateChanged,
              .created_at_unix_ms = 100,
              .last_status_at_unix_ms = 200,
              .last_output_at_unix_ms = 210,
              .last_activity_at_unix_ms = 220,
              .last_file_change_at_unix_ms = std::nullopt,
              .last_git_change_at_unix_ms = 220,
              .last_controller_change_at_unix_ms = std::nullopt,
              .attention_since_unix_ms = 220,
              .pty_columns = std::nullopt,
              .pty_rows = std::nullopt,
              .current_sequence = 12,
              .attached_client_count = 2,
              .recent_file_change_count = 3,
              .git_dirty = true,
              .git_branch = "main",
              .git_modified_count = 1,
              .git_staged_count = 1,
              .git_untracked_count = 1,
          },
  });

  EXPECT_NE(json.find("\"type\":\"session.activity\""), std::string::npos);
  EXPECT_NE(json.find("\"sessionId\":\"s_9\""), std::string::npos);
  EXPECT_NE(json.find("\"activityState\":\"active\""), std::string::npos);
  EXPECT_NE(json.find("\"groupTags\":[\"frontend\"]"), std::string::npos);
  EXPECT_NE(json.find("\"supervisionState\":\"active\""), std::string::npos);
  EXPECT_NE(json.find("\"attentionState\":\"info\""), std::string::npos);
  EXPECT_NE(json.find("\"attentionReason\":\"git_state_changed\""), std::string::npos);
  EXPECT_NE(json.find("\"lastOutputAtUnixMs\":210"), std::string::npos);
  EXPECT_NE(json.find("\"lastActivityAtUnixMs\":220"), std::string::npos);
  EXPECT_NE(json.find("\"currentSequence\":12"), std::string::npos);
  EXPECT_NE(json.find("\"attachedClientCount\":2"), std::string::npos);
  EXPECT_NE(json.find("\"recentFileChangeCount\":3"), std::string::npos);
  EXPECT_NE(json.find("\"gitDirty\":true"), std::string::npos);
  EXPECT_NE(json.find("\"gitBranch\":\"main\""), std::string::npos);
  EXPECT_NE(json.find("\"gitModifiedCount\":1"), std::string::npos);
  EXPECT_NE(json.find("\"gitStagedCount\":1"), std::string::npos);
  EXPECT_NE(json.find("\"gitUntrackedCount\":1"), std::string::npos);
}

TEST(HttpJsonTest, SerializesSessionInventoryEvent) {
  const std::string json = ToJson(SessionInventoryEvent{
      .sessions =
          {
              vibe::service::SessionSummary{
                  .id = *vibe::session::SessionId::TryCreate("s_1"),
                  .provider = vibe::session::ProviderType::Codex,
                  .workspace_root = ".",
                  .title = "one",
                  .status = vibe::session::SessionStatus::Running,
                  .conversation_id = std::nullopt,
                  .group_tags = {"frontend"},
                  .controller_client_id = std::nullopt,
                  .controller_kind = vibe::session::ControllerKind::Host,
                  .is_recovered = false,
                  .is_active = true,
                  .supervision_state = vibe::session::SupervisionState::Active,
                  .attention_state = vibe::session::AttentionState::None,
                  .attention_reason = vibe::session::AttentionReason::None,
                  .created_at_unix_ms = std::nullopt,
                  .last_status_at_unix_ms = std::nullopt,
                  .last_output_at_unix_ms = std::nullopt,
                  .last_activity_at_unix_ms = std::nullopt,
                  .last_file_change_at_unix_ms = std::nullopt,
                  .last_git_change_at_unix_ms = std::nullopt,
                  .last_controller_change_at_unix_ms = std::nullopt,
                  .attention_since_unix_ms = std::nullopt,
                  .pty_columns = std::nullopt,
                  .pty_rows = std::nullopt,
                  .current_sequence = 2,
                  .recent_file_change_count = 0,
                  .git_dirty = true,
                  .git_branch = "main",
              },
              vibe::service::SessionSummary{
                  .id = *vibe::session::SessionId::TryCreate("s_2"),
                  .provider = vibe::session::ProviderType::Claude,
                  .workspace_root = "/tmp",
                  .title = "two",
                  .status = vibe::session::SessionStatus::Exited,
                  .conversation_id = std::nullopt,
                  .group_tags = {"backend"},
                  .controller_client_id = std::nullopt,
                  .controller_kind = vibe::session::ControllerKind::None,
                  .is_recovered = true,
                  .is_active = false,
                  .supervision_state = vibe::session::SupervisionState::Stopped,
                  .attention_state = vibe::session::AttentionState::None,
                  .attention_reason = vibe::session::AttentionReason::None,
                  .created_at_unix_ms = std::nullopt,
                  .last_status_at_unix_ms = std::nullopt,
                  .last_output_at_unix_ms = std::nullopt,
                  .last_activity_at_unix_ms = std::nullopt,
                  .last_file_change_at_unix_ms = std::nullopt,
                  .last_git_change_at_unix_ms = std::nullopt,
                  .last_controller_change_at_unix_ms = std::nullopt,
                  .attention_since_unix_ms = std::nullopt,
                  .pty_columns = std::nullopt,
                  .pty_rows = std::nullopt,
                  .current_sequence = 4,
                  .recent_file_change_count = 1,
                  .git_dirty = false,
                  .git_branch = "feature",
              },
          },
  });

  EXPECT_NE(json.find("\"type\":\"sessions.snapshot\""), std::string::npos);
  EXPECT_NE(json.find("\"sessionId\":\"s_1\""), std::string::npos);
  EXPECT_NE(json.find("\"sessionId\":\"s_2\""), std::string::npos);
  EXPECT_NE(json.find("\"groupTags\":[\"frontend\"]"), std::string::npos);
  EXPECT_NE(json.find("\"groupTags\":[\"backend\"]"), std::string::npos);
  EXPECT_NE(json.find("\"inventoryState\":\"live\""), std::string::npos);
  EXPECT_NE(json.find("\"inventoryState\":\"archived\""), std::string::npos);
}

TEST(HttpJsonTest, SerializesErrorEvent) {
  const std::string json = ToJson(ErrorEvent{
      .session_id = "s_9",
      .code = "invalid_command",
      .message = "invalid websocket command",
  });

  EXPECT_NE(json.find("\"type\":\"error\""), std::string::npos);
  EXPECT_NE(json.find("\"sessionId\":\"s_9\""), std::string::npos);
  EXPECT_NE(json.find("\"code\":\"invalid_command\""), std::string::npos);
  EXPECT_NE(json.find("\"message\":\"invalid websocket command\""), std::string::npos);
}

TEST(HttpJsonTest, SerializesAttachedClientInfo) {
  const std::string json = ToJson(AttachedClientInfo{
      .client_id = "ws_s_1_100",
      .session_id = "s_1",
      .session_title = "demo",
      .client_address = "127.0.0.1",
      .session_status = vibe::session::SessionStatus::Running,
      .session_is_recovered = false,
      .claimed_kind = vibe::session::ControllerKind::Remote,
      .is_local = false,
      .has_control = true,
      .connected_at_unix_ms = 300,
  });

  EXPECT_NE(json.find("\"clientId\":\"ws_s_1_100\""), std::string::npos);
  EXPECT_NE(json.find("\"sessionId\":\"s_1\""), std::string::npos);
  EXPECT_NE(json.find("\"sessionTitle\":\"demo\""), std::string::npos);
  EXPECT_NE(json.find("\"sessionStatus\":\"Running\""), std::string::npos);
  EXPECT_NE(json.find("\"sessionArchivedRecord\":false"), std::string::npos);
  EXPECT_NE(json.find("\"claimedKind\":\"remote\""), std::string::npos);
  EXPECT_NE(json.find("\"hasControl\":true"), std::string::npos);
  EXPECT_NE(json.find("\"connectedAtUnixMs\":300"), std::string::npos);
}

}  // namespace
}  // namespace vibe::net
