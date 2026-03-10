#include <gtest/gtest.h>

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

}  // namespace
}  // namespace vibe::service
