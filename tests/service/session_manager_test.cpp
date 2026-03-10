#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

#include "vibe/service/session_manager.h"

namespace vibe::service {
namespace {

class FakeSessionStore final : public vibe::store::SessionStore {
 public:
  [[nodiscard]] auto LoadSessions() const -> std::vector<vibe::store::PersistedSessionRecord> override {
    return sessions;
  }

  [[nodiscard]] auto UpsertSessionRecord(const vibe::store::PersistedSessionRecord& record) -> bool override {
    upserted.push_back(record);
    return true;
  }

  [[nodiscard]] auto RemoveSessionRecord(const std::string& /*session_id*/) -> bool override {
    return true;
  }

  std::vector<vibe::store::PersistedSessionRecord> sessions;
  mutable std::vector<vibe::store::PersistedSessionRecord> upserted;
};

auto FindLastPersistedRecord(const FakeSessionStore& session_store, const std::string& session_id)
    -> std::optional<vibe::store::PersistedSessionRecord> {
  for (auto it = session_store.upserted.rbegin(); it != session_store.upserted.rend(); ++it) {
    if (it->session_id == session_id) {
      return *it;
    }
  }
  return std::nullopt;
}

TEST(SessionManagerTest, LoadsPersistedSessionsAsRecoveredExitedSessions) {
  FakeSessionStore session_store;
  session_store.sessions.push_back(vibe::store::PersistedSessionRecord{
      .session_id = "s_42",
      .provider = vibe::session::ProviderType::Claude,
      .workspace_root = "/tmp/recovered",
      .title = "recovered-session",
      .status = vibe::session::SessionStatus::Running,
      .current_sequence = 7,
      .recent_terminal_tail = "restored tail",
  });

  SessionManager manager(&session_store);

  EXPECT_EQ(manager.LoadPersistedSessions(), 1U);

  const auto summary = manager.GetSession("s_42");
  ASSERT_TRUE(summary.has_value());
  EXPECT_EQ(summary->provider, vibe::session::ProviderType::Claude);
  EXPECT_EQ(summary->workspace_root, "/tmp/recovered");
  EXPECT_EQ(summary->title, "recovered-session");
  EXPECT_EQ(summary->status, vibe::session::SessionStatus::Exited);
  EXPECT_EQ(summary->controller_kind, vibe::session::ControllerKind::Host);
  EXPECT_FALSE(summary->controller_client_id.has_value());

  const auto snapshot = manager.GetSnapshot("s_42");
  ASSERT_TRUE(snapshot.has_value());
  EXPECT_EQ(snapshot->metadata.status, vibe::session::SessionStatus::Exited);
  EXPECT_EQ(snapshot->current_sequence, 7U);
  EXPECT_EQ(snapshot->recent_terminal_tail, "restored tail");

  const auto tail = manager.GetTail("s_42", 64);
  ASSERT_TRUE(tail.has_value());
  EXPECT_EQ(tail->seq_start, 7U);
  EXPECT_EQ(tail->seq_end, 7U);
  EXPECT_EQ(tail->data, "restored tail");

  const auto output = manager.GetOutputSince("s_42", 1);
  ASSERT_TRUE(output.has_value());
  EXPECT_EQ(output->seq_start, 7U);
  EXPECT_EQ(output->seq_end, 7U);
  EXPECT_EQ(output->data, "restored tail");

  EXPECT_FALSE(manager.RequestControl("s_42", "client-1", vibe::session::ControllerKind::Remote));
  EXPECT_FALSE(manager.SendInput("s_42", "ignored"));
  EXPECT_FALSE(manager.ResizeSession("s_42", vibe::session::TerminalSize{.columns = 80, .rows = 24}));
  EXPECT_TRUE(manager.StopSession("s_42"));
}

TEST(SessionManagerTest, SkipsInvalidPersistedSessionIds) {
  FakeSessionStore session_store;
  session_store.sessions.push_back(vibe::store::PersistedSessionRecord{
      .session_id = "bad id",
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = ".",
      .title = "bad-session",
      .status = vibe::session::SessionStatus::Exited,
      .current_sequence = 0,
      .recent_terminal_tail = "",
  });

  SessionManager manager(&session_store);

  EXPECT_EQ(manager.LoadPersistedSessions(), 0U);
  EXPECT_TRUE(manager.ListSessions().empty());
}

TEST(SessionManagerTest, CreateSessionAllocatesPastHighestRecoveredAndLiveSessionId) {
  FakeSessionStore session_store;
  session_store.sessions.push_back(vibe::store::PersistedSessionRecord{
      .session_id = "s_2",
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = "/tmp/recovered-two",
      .title = "recovered-two",
      .status = vibe::session::SessionStatus::Exited,
      .current_sequence = 2,
      .recent_terminal_tail = "tail-2",
  });
  session_store.sessions.push_back(vibe::store::PersistedSessionRecord{
      .session_id = "s_9",
      .provider = vibe::session::ProviderType::Claude,
      .workspace_root = "/tmp/recovered-nine",
      .title = "recovered-nine",
      .status = vibe::session::SessionStatus::Running,
      .current_sequence = 9,
      .recent_terminal_tail = "tail-9",
  });

  SessionManager manager(&session_store);
  EXPECT_EQ(manager.LoadPersistedSessions(), 2U);

  const auto first_created = manager.CreateSession(CreateSessionRequest{
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = ".",
      .title = "live-one",
      .command_argv = std::nullopt,
  });
  ASSERT_TRUE(first_created.has_value());
  EXPECT_EQ(first_created->id.value(), "s_10");

  const auto second_created = manager.CreateSession(CreateSessionRequest{
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = ".",
      .title = "live-two",
      .command_argv = std::nullopt,
  });
  ASSERT_TRUE(second_created.has_value());
  EXPECT_EQ(second_created->id.value(), "s_11");

  const auto sessions = manager.ListSessions();
  ASSERT_EQ(sessions.size(), 4U);
  EXPECT_EQ(sessions[0].id.value(), "s_2");
  EXPECT_EQ(sessions[1].id.value(), "s_9");
  EXPECT_EQ(sessions[2].id.value(), "s_10");
  EXPECT_EQ(sessions[3].id.value(), "s_11");
}

TEST(SessionManagerTest, ShutdownTerminatesLiveSessionsClearsControlAndPersistsExitedState) {
  FakeSessionStore session_store;
  SessionManager manager(&session_store);

  const auto created = manager.CreateSession(CreateSessionRequest{
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = ".",
      .title = "shutdown-target",
      .command_argv = std::vector<std::string>{"/bin/sh", "-c", "sleep 30"},
  });
  ASSERT_TRUE(created.has_value());
  ASSERT_TRUE(manager.RequestControl(created->id.value(), "remote-1",
                                     vibe::session::ControllerKind::Remote));

  EXPECT_EQ(manager.Shutdown(), 1U);

  const auto summary = manager.GetSession(created->id.value());
  ASSERT_TRUE(summary.has_value());
  EXPECT_EQ(summary->status, vibe::session::SessionStatus::Exited);
  EXPECT_EQ(summary->controller_kind, vibe::session::ControllerKind::Host);
  EXPECT_FALSE(summary->controller_client_id.has_value());

  const auto persisted = FindLastPersistedRecord(session_store, created->id.value());
  ASSERT_TRUE(persisted.has_value());
  EXPECT_EQ(persisted->status, vibe::session::SessionStatus::Exited);
}

}  // namespace
}  // namespace vibe::service
