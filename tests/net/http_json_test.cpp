#include <gtest/gtest.h>

#ifndef SENTRITS_VERSION
#define SENTRITS_VERSION "0.0.0"
#endif

#include <boost/json.hpp>

#include "vibe/net/json.h"

namespace vibe::net {
namespace {

namespace json = boost::json;

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
  EXPECT_NE(json.find("\"launchRecordCount\":0"), std::string::npos);
  EXPECT_NE(json.find(std::string("\"version\":\"") + SENTRITS_VERSION + "\""), std::string::npos);
  EXPECT_NE(json.find("\"capabilities\":[\"sessions\",\"rest\",\"websocket\",\"launchRecords\"]"),
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

TEST(HttpJsonTest, SerializesEffectiveEnvironmentWithRedaction) {
  const vibe::session::EffectiveEnvironment env{
      .entries =
          {
              vibe::session::EnvEntry{
                  .key = "OPENAI_API_KEY",
                  .value = "secret-value",
                  .source = vibe::session::EnvSource::BootstrapShell,
              },
              vibe::session::EnvEntry{
                  .key = "PATH",
                  .value = "/usr/bin:/bin",
                  .source = vibe::session::EnvSource::EnvFile,
              },
          },
      .mode = vibe::session::EnvMode::BootstrapFromShell,
      .bootstrap_shell_path = "/tmp/bootstrap-shell",
      .env_file_path = "/tmp/project/.env",
      .bootstrap_warning = "bootstrap emitted stderr",
  };

  const std::string redacted = ToJson(env, /*redact=*/true);
  EXPECT_NE(redacted.find("\"mode\":\"bootstrap_from_shell\""), std::string::npos);
  EXPECT_NE(redacted.find("\"bootstrapShellPath\":\"/tmp/bootstrap-shell\""), std::string::npos);
  EXPECT_NE(redacted.find("\"envFilePath\":\"/tmp/project/.env\""), std::string::npos);
  EXPECT_NE(redacted.find("\"bootstrapWarning\":\"bootstrap emitted stderr\""), std::string::npos);
  EXPECT_NE(redacted.find("\"key\":\"OPENAI_API_KEY\""), std::string::npos);
  EXPECT_NE(redacted.find("\"value\":\"<redacted>\""), std::string::npos);
  EXPECT_EQ(redacted.find("secret-value"), std::string::npos);
  EXPECT_NE(redacted.find("\"redacted\":true"), std::string::npos);
  EXPECT_NE(redacted.find("\"key\":\"PATH\""), std::string::npos);
  EXPECT_NE(redacted.find("\"value\":\"/usr/bin:/bin\""), std::string::npos);

  const std::string plain = ToJson(env, /*redact=*/false);
  EXPECT_NE(plain.find("\"value\":\"secret-value\""), std::string::npos);
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
      .interaction_kind = vibe::session::SessionInteractionKind::RunningNonInteractive,
      .semantic_preview = "",
      .node_summary =
          vibe::session::SessionNodeSummary{
              .session_id = "s_host",
              .lifecycle_status = vibe::session::SessionStatus::Running,
              .interaction_kind = vibe::session::SessionInteractionKind::RunningNonInteractive,
              .attention_state = vibe::session::AttentionState::Info,
              .semantic_preview = "",
              .recent_file_change_count = 0,
              .git_dirty = false,
              .last_activity_at_unix_ms = 220,
          },
      .git_dirty = false,
      .git_branch = "",
      .git_modified_count = 0,
      .git_staged_count = 0,
      .git_untracked_count = 0,
      .mode =
          vibe::session::SessionModeSummary{
              .lifecycle_status = vibe::session::SessionStatus::Running,
              .interaction_kind = vibe::session::SessionInteractionKind::RunningNonInteractive,
              .activity_state = vibe::session::SessionActivityState::MeaningfulOutput,
          },
      .attention =
          vibe::session::SessionAttentionSummary{
              .level = vibe::session::AttentionState::Info,
              .cause = vibe::session::AttentionReason::WorkspaceChanged,
              .since_unix_ms = 220,
              .summary = "Workspace changed",
          },
  });

  EXPECT_NE(json.find("\"controllerKind\":\"host\""), std::string::npos);
  EXPECT_NE(json.find("\"conversationId\":\"conv_hash_1\""), std::string::npos);
  EXPECT_NE(json.find("\"groupTags\":[\"frontend\",\"mvp\"]"), std::string::npos);
  EXPECT_NE(json.find("\"archivedRecord\":false"), std::string::npos);
  EXPECT_NE(json.find("\"inventoryState\":\"live\""), std::string::npos);
  EXPECT_NE(json.find("\"attentionState\":\"info\""), std::string::npos);
  EXPECT_NE(json.find("\"attentionReason\":\"workspace_changed\""), std::string::npos);
  EXPECT_NE(json.find("\"mode\":{"), std::string::npos);
  EXPECT_NE(json.find("\"attention\":{"), std::string::npos);
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

TEST(HttpJsonTest, SerializesEvidenceResult) {
  const auto session_id = vibe::session::SessionId::TryCreate("log_001");
  ASSERT_TRUE(session_id.has_value());
  const vibe::service::EvidenceSourceRef source{
      .kind = vibe::service::EvidenceSourceKind::ManagedLogSession,
      .session_id = *session_id,
  };

  const std::string payload = ToJson(vibe::service::EvidenceResult{
      .source = source,
      .operation = vibe::service::EvidenceOperation::Search,
      .query = "timeout",
      .revision_start = 10,
      .revision_end = 12,
      .oldest_revision = 4,
      .latest_revision = 15,
      .entries =
          {
              vibe::service::EvidenceEntry{
                  .entry_id = "log:log_001:rev:11",
                  .source = source,
                  .revision = 11,
                  .byte_start = 120,
                  .byte_end = 151,
                  .timestamp_unix_ms = 123456,
                  .stream = vibe::service::LogStream::Stderr,
                  .text = "encoder timeout after 5000ms",
                  .partial = false,
              },
          },
      .highlights =
          {
              vibe::service::EvidenceHighlight{
                  .entry_id = "log:log_001:rev:11",
                  .start = 8,
                  .length = 7,
                  .kind = vibe::service::EvidenceHighlightKind::Match,
              },
          },
      .truncated = true,
      .buffer_exhausted = false,
      .dropped_entries = 2,
      .dropped_bytes = 64,
      .error_code = "",
      .replay_token = "token_1",
  });

  const auto parsed = json::parse(payload).as_object();
  EXPECT_EQ(json::value_to<std::string>(parsed.at("operation")), "search");
  EXPECT_EQ(json::value_to<std::string>(parsed.at("query")), "timeout");
  EXPECT_EQ(parsed.at("revisionStart").as_int64(), 10);
  EXPECT_EQ(parsed.at("oldestRevision").as_int64(), 4);
  EXPECT_TRUE(parsed.at("truncated").as_bool());
  EXPECT_FALSE(parsed.at("bufferExhausted").as_bool());
  EXPECT_EQ(parsed.at("droppedEntries").as_int64(), 2);
  EXPECT_FALSE(parsed.contains("errorCode"));
  EXPECT_EQ(json::value_to<std::string>(parsed.at("replayToken")), "token_1");

  const auto& source_json = parsed.at("source").as_object();
  EXPECT_EQ(json::value_to<std::string>(source_json.at("kind")), "managed_log_session");
  EXPECT_EQ(json::value_to<std::string>(source_json.at("sessionId")), "log_001");

  const auto& entry = parsed.at("entries").as_array().front().as_object();
  EXPECT_EQ(json::value_to<std::string>(entry.at("entryId")), "log:log_001:rev:11");
  EXPECT_EQ(json::value_to<std::string>(entry.at("stream")), "stderr");
  EXPECT_EQ(json::value_to<std::string>(entry.at("text")), "encoder timeout after 5000ms");
  EXPECT_EQ(entry.at("timestampUnixMs").as_int64(), 123456);
  EXPECT_FALSE(entry.at("partial").as_bool());

  const auto& highlight = parsed.at("highlights").as_array().front().as_object();
  EXPECT_EQ(json::value_to<std::string>(highlight.at("entryId")), "log:log_001:rev:11");
  EXPECT_EQ(highlight.at("start").as_int64(), 8);
  EXPECT_EQ(highlight.at("length").as_int64(), 7);
  EXPECT_EQ(json::value_to<std::string>(highlight.at("kind")), "match");
}

TEST(HttpJsonTest, SerializesEvidenceBufferExhaustedResult) {
  const auto session_id = vibe::session::SessionId::TryCreate("log_002");
  ASSERT_TRUE(session_id.has_value());

  const std::string payload = ToJson(vibe::service::EvidenceResult{
      .source =
          vibe::service::EvidenceSourceRef{
              .kind = vibe::service::EvidenceSourceKind::ManagedLogSession,
              .session_id = *session_id,
          },
      .operation = vibe::service::EvidenceOperation::Context,
      .revision_start = 0,
      .revision_end = 0,
      .oldest_revision = 100,
      .latest_revision = 120,
      .buffer_exhausted = true,
      .error_code = "buffer_exhausted",
      .replay_token = "token_2",
  });

  const auto parsed = json::parse(payload).as_object();
  EXPECT_EQ(json::value_to<std::string>(parsed.at("operation")), "context");
  EXPECT_TRUE(parsed.at("bufferExhausted").as_bool());
  EXPECT_EQ(json::value_to<std::string>(parsed.at("errorCode")), "buffer_exhausted");
}

TEST(HttpJsonTest, SerializesObservationEventWithActorAttribution) {
  const auto session_id = vibe::session::SessionId::TryCreate("log_003");
  ASSERT_TRUE(session_id.has_value());

  const std::string payload = ToJson(vibe::service::ObservationEvent{
      .id = "obs_1",
      .timestamp_unix_ms = 456789,
      .actor_session_id = "agent_1",
      .actor_title = "Codex",
      .actor_id = "agent_1",
      .pid = 111,
      .uid = 501,
      .gid = 20,
      .operation = vibe::service::EvidenceOperation::Tail,
      .source =
          vibe::service::EvidenceSourceRef{
              .kind = vibe::service::EvidenceSourceKind::ManagedLogSession,
              .session_id = *session_id,
          },
      .source_title = "Runtime Log",
      .query = "",
      .revision_start = 30,
      .revision_end = 40,
      .result_count = 11,
      .truncated = false,
      .summary = "Codex tailed 11 lines from Runtime Log",
      .replay_token = "token_3",
  });

  const auto parsed = json::parse(payload).as_object();
  EXPECT_EQ(json::value_to<std::string>(parsed.at("id")), "obs_1");
  EXPECT_EQ(parsed.at("timestampUnixMs").as_int64(), 456789);
  EXPECT_EQ(json::value_to<std::string>(parsed.at("actorSessionId")), "agent_1");
  EXPECT_EQ(json::value_to<std::string>(parsed.at("actorTitle")), "Codex");
  EXPECT_EQ(json::value_to<std::string>(parsed.at("actorId")), "agent_1");
  EXPECT_EQ(parsed.at("pid").as_int64(), 111);
  EXPECT_EQ(parsed.at("uid").as_int64(), 501);
  EXPECT_EQ(parsed.at("gid").as_int64(), 20);
  EXPECT_EQ(json::value_to<std::string>(parsed.at("operation")), "tail");
  EXPECT_EQ(json::value_to<std::string>(parsed.at("sourceTitle")), "Runtime Log");
  EXPECT_EQ(parsed.at("resultCount").as_int64(), 11);
  EXPECT_EQ(json::value_to<std::string>(parsed.at("summary")),
            "Codex tailed 11 lines from Runtime Log");
  EXPECT_EQ(json::value_to<std::string>(parsed.at("replayToken")), "token_3");
}

TEST(HttpJsonTest, OmitsUnknownObservationProcessIdentity) {
  const auto session_id = vibe::session::SessionId::TryCreate("log_003");
  ASSERT_TRUE(session_id.has_value());

  const std::string payload = ToJson(vibe::service::ObservationEvent{
      .id = "obs_1",
      .actor_session_id = "agent_1",
      .actor_id = "agent_1",
      .operation = vibe::service::EvidenceOperation::Tail,
      .source =
          vibe::service::EvidenceSourceRef{
              .kind = vibe::service::EvidenceSourceKind::ManagedLogSession,
              .session_id = *session_id,
          },
  });

  const auto parsed = json::parse(payload).as_object();
  EXPECT_FALSE(parsed.contains("pid"));
  EXPECT_FALSE(parsed.contains("uid"));
  EXPECT_FALSE(parsed.contains("gid"));
}

TEST(HttpJsonTest, SerializesObservationCreatedEvent) {
  const auto session_id = vibe::session::SessionId::TryCreate("log_004");
  ASSERT_TRUE(session_id.has_value());

  const std::string payload = ToJson(ObservationCreatedEvent{
      .event =
          vibe::service::ObservationEvent{
              .id = "obs_2",
              .actor_session_id = "agent_2",
              .actor_id = "agent_2",
              .operation = vibe::service::EvidenceOperation::Search,
              .source =
                  vibe::service::EvidenceSourceRef{
                      .kind = vibe::service::EvidenceSourceKind::ManagedLogSession,
                      .session_id = *session_id,
                  },
              .source_title = "Runtime Log",
              .query = "error",
              .result_count = 3,
              .replay_token = "token_4",
          },
  });

  const auto parsed = json::parse(payload).as_object();
  EXPECT_EQ(json::value_to<std::string>(parsed.at("type")), "observation.created");
  const auto& event = parsed.at("event").as_object();
  EXPECT_EQ(json::value_to<std::string>(event.at("id")), "obs_2");
  EXPECT_EQ(json::value_to<std::string>(event.at("operation")), "search");
  EXPECT_EQ(json::value_to<std::string>(event.at("replayToken")), "token_4");
}

TEST(HttpJsonTest, SerializesObservationEventList) {
  const auto session_id = vibe::session::SessionId::TryCreate("log_004");
  ASSERT_TRUE(session_id.has_value());

  const std::string payload = ToJson(std::vector<vibe::service::ObservationEvent>{
      vibe::service::ObservationEvent{
          .id = "obs_a",
          .actor_session_id = "agent_a",
          .actor_id = "agent_a",
          .operation = vibe::service::EvidenceOperation::Search,
          .source =
              vibe::service::EvidenceSourceRef{
                  .kind = vibe::service::EvidenceSourceKind::ManagedLogSession,
                  .session_id = *session_id,
              },
          .query = "error",
          .result_count = 3,
          .replay_token = "token_a",
      },
  });

  const auto parsed = json::parse(payload).as_array();
  ASSERT_EQ(parsed.size(), 1U);
  const auto& event = parsed.front().as_object();
  EXPECT_EQ(json::value_to<std::string>(event.at("operation")), "search");
  EXPECT_EQ(json::value_to<std::string>(event.at("actorSessionId")), "agent_a");
  EXPECT_EQ(json::value_to<std::string>(event.at("query")), "error");
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
      .terminal_screen =
          vibe::session::TerminalScreenSnapshot{
              .columns = 80,
              .rows = 24,
              .render_revision = 7,
              .cursor_row = 1,
              .cursor_column = 2,
              .visible_lines = {"visible"},
              .scrollback_lines = {"history"},
              .bootstrap_ansi = "\x1b[2J\x1b[Hvisible",
          },
      .signals =
          vibe::session::SessionSignals{
              .last_raw_output_at_unix_ms = 95,
              .last_meaningful_output_at_unix_ms = 100,
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
              .interaction_kind = vibe::session::SessionInteractionKind::RunningNonInteractive,
              .terminal_semantic_change =
                  vibe::session::TerminalSemanticChange{
                      .kind = vibe::session::TerminalSemanticChangeKind::CosmeticChurn,
                      .changed_visible_line_count = 1,
                      .scrollback_lines_added = 0,
                      .appended_visible_character_count = 1,
                      .cursor_moved = false,
                      .alt_screen_entered = false,
                      .alt_screen_exited = false,
                  },
              .git_dirty = true,
              .git_branch = "main",
              .git_modified_count = 1,
              .git_staged_count = 1,
              .git_untracked_count = 1,
              .mode =
                  vibe::session::SessionModeSummary{
                      .lifecycle_status = vibe::session::SessionStatus::Running,
                      .interaction_kind = vibe::session::SessionInteractionKind::RunningNonInteractive,
                      .activity_state = vibe::session::SessionActivityState::MeaningfulOutput,
                  },
              .attention =
                  vibe::session::SessionAttentionSummary{
                      .level = vibe::session::AttentionState::Info,
                      .cause = vibe::session::AttentionReason::WorkspaceChanged,
                      .since_unix_ms = 110,
                      .summary = "Workspace changed",
                  },
          },
      .node_summary =
          vibe::session::SessionNodeSummary{
              .session_id = "snapshot_001",
              .lifecycle_status = vibe::session::SessionStatus::Running,
              .interaction_kind = vibe::session::SessionInteractionKind::RunningNonInteractive,
              .attention_state = vibe::session::AttentionState::Info,
              .semantic_preview = "Workspace changed",
              .recent_file_change_count = 2,
              .git_dirty = true,
              .last_activity_at_unix_ms = 110,
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
  EXPECT_NE(json.find("\"lastRawOutputAtUnixMs\":95"), std::string::npos);
  EXPECT_NE(json.find("\"lastMeaningfulOutputAtUnixMs\":100"), std::string::npos);
  EXPECT_NE(json.find("\"lastActivityAtUnixMs\":110"), std::string::npos);
  EXPECT_NE(json.find("\"currentSequence\":42"), std::string::npos);
  EXPECT_NE(json.find("\"bootstrapAnsi\":\"\\u001b[2J\\u001b[Hvisible\""), std::string::npos);
  EXPECT_NE(json.find("\"recentFileChangeCount\":2"), std::string::npos);
  EXPECT_NE(json.find("\"supervisionState\":\"active\""), std::string::npos);
  EXPECT_NE(json.find("\"attentionState\":\"info\""), std::string::npos);
  EXPECT_NE(json.find("\"mode\":{"), std::string::npos);
  EXPECT_NE(json.find("\"attention\":{"), std::string::npos);
  EXPECT_NE(json.find("\"signals\""), std::string::npos);
  EXPECT_NE(json.find("\"attentionReason\":\"workspace_changed\""), std::string::npos);
  EXPECT_NE(json.find("\"terminalSemanticChange\":{\"kind\":\"cosmetic_churn\""), std::string::npos);
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
              .interaction_kind = vibe::session::SessionInteractionKind::InteractiveLineMode,
              .semantic_preview = "Controller changed",
              .node_summary =
                  vibe::session::SessionNodeSummary{
                      .session_id = "s_9",
                      .lifecycle_status = vibe::session::SessionStatus::Running,
                      .interaction_kind = vibe::session::SessionInteractionKind::InteractiveLineMode,
                      .attention_state = vibe::session::AttentionState::Info,
                      .semantic_preview = "Controller changed",
                      .recent_file_change_count = 0,
                      .git_dirty = false,
                      .last_activity_at_unix_ms = 220,
                  },
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
              .interaction_kind = vibe::session::SessionInteractionKind::InteractiveLineMode,
              .semantic_preview = "Git state changed",
              .node_summary =
                  vibe::session::SessionNodeSummary{
                      .session_id = "s_9",
                      .lifecycle_status = vibe::session::SessionStatus::Running,
                      .interaction_kind = vibe::session::SessionInteractionKind::InteractiveLineMode,
                      .attention_state = vibe::session::AttentionState::Info,
                      .semantic_preview = "Git state changed",
                      .recent_file_change_count = 3,
                      .git_dirty = true,
                      .last_activity_at_unix_ms = 220,
                  },
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
                  .interaction_kind = vibe::session::SessionInteractionKind::RunningNonInteractive,
                  .semantic_preview = "",
                  .node_summary =
                      vibe::session::SessionNodeSummary{
                          .session_id = "s_1",
                          .lifecycle_status = vibe::session::SessionStatus::Running,
                          .interaction_kind = vibe::session::SessionInteractionKind::RunningNonInteractive,
                          .attention_state = vibe::session::AttentionState::None,
                          .semantic_preview = "",
                          .recent_file_change_count = 0,
                          .git_dirty = true,
                          .last_activity_at_unix_ms = std::nullopt,
                      },
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
                  .interaction_kind = vibe::session::SessionInteractionKind::CompletedQuickly,
                  .semantic_preview = "",
                  .node_summary =
                      vibe::session::SessionNodeSummary{
                          .session_id = "s_2",
                          .lifecycle_status = vibe::session::SessionStatus::Exited,
                          .interaction_kind = vibe::session::SessionInteractionKind::CompletedQuickly,
                          .attention_state = vibe::session::AttentionState::None,
                          .semantic_preview = "",
                          .recent_file_change_count = 1,
                          .git_dirty = false,
                          .last_activity_at_unix_ms = std::nullopt,
                      },
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
