#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
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

  [[nodiscard]] auto RemoveSessionRecord(const std::string& session_id) -> bool override {
    removed.push_back(session_id);
    return true;
  }

  std::vector<vibe::store::PersistedSessionRecord> sessions;
  mutable std::vector<vibe::store::PersistedSessionRecord> upserted;
  mutable std::vector<std::string> removed;
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

auto PollUntilStatus(SessionManager& manager, const std::string& session_id,
                     const vibe::session::SessionStatus expected_status,
                     const int attempts = 40) -> std::optional<SessionSummary> {
  for (int index = 0; index < attempts; ++index) {
    manager.PollAll(10);
    const auto summary = manager.GetSession(session_id);
    if (summary.has_value() && summary->status == expected_status) {
      return summary;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  return manager.GetSession(session_id);
}

class GitSessionManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = std::filesystem::temp_directory_path() /
                ("vibe session manager git test " +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(test_dir_);
    std::filesystem::create_directories(test_dir_);

    RunGit("init");
    RunGit("config user.email \"test@example.com\"");
    RunGit("config user.name \"Test User\"");

    WriteFile("tracked.txt", "tracked\n");
    RunGit("add tracked.txt");
    RunGit("commit -m \"initial commit\"");
  }

  void TearDown() override {
    std::filesystem::remove_all(test_dir_);
  }

  void RunGit(const std::string& command) const {
    const std::string full_command = "git -C '" + test_dir_.string() + "' " + command;
    ASSERT_EQ(std::system(full_command.c_str()), 0);
  }

  void WriteFile(const std::string& name, const std::string& contents) const {
    std::ofstream stream(test_dir_ / name);
    stream << contents;
  }

  void PollUntilGitCheck(SessionManager& manager, const int batches = 100) const {
    for (int index = 0; index < batches; ++index) {
      manager.PollAll(0);
    }
  }

  std::filesystem::path test_dir_;
};

TEST(SessionManagerTest, LoadsPersistedSessionsAsRecoveredExitedSessions) {
  FakeSessionStore session_store;
  session_store.sessions.push_back(vibe::store::PersistedSessionRecord{
      .session_id = "s_42",
      .provider = vibe::session::ProviderType::Claude,
      .workspace_root = "/tmp/recovered",
      .title = "recovered-session",
      .status = vibe::session::SessionStatus::Running,
      .conversation_id = std::nullopt,
      .group_tags = {"frontend", "mvp"},
      .current_sequence = 7,
      .recent_terminal_tail = "restored tail",
  });

  SessionManager manager(&session_store, vibe::session::CreatePlatformPtyProcess,
                         std::chrono::milliseconds(1), std::chrono::milliseconds(1));

  EXPECT_EQ(manager.LoadPersistedSessions(), 1U);

  const auto summary = manager.GetSession("s_42");
  ASSERT_TRUE(summary.has_value());
  EXPECT_EQ(summary->provider, vibe::session::ProviderType::Claude);
  EXPECT_EQ(summary->workspace_root, "/tmp/recovered");
  EXPECT_EQ(summary->title, "recovered-session");
  EXPECT_EQ(summary->status, vibe::session::SessionStatus::Exited);
  EXPECT_EQ(summary->group_tags, (std::vector<std::string>{"frontend", "mvp"}));
  EXPECT_EQ(summary->controller_kind, vibe::session::ControllerKind::Host);
  EXPECT_TRUE(summary->is_recovered);
  EXPECT_FALSE(summary->is_active);
  EXPECT_FALSE(summary->created_at_unix_ms.has_value());
  EXPECT_FALSE(summary->last_status_at_unix_ms.has_value());
  EXPECT_FALSE(summary->controller_client_id.has_value());

  const auto snapshot = manager.GetSnapshot("s_42");
  ASSERT_TRUE(snapshot.has_value());
  EXPECT_EQ(snapshot->metadata.status, vibe::session::SessionStatus::Exited);
  EXPECT_EQ(snapshot->metadata.group_tags, (std::vector<std::string>{"frontend", "mvp"}));
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
      .conversation_id = std::nullopt,
      .group_tags = {},
      .current_sequence = 0,
      .recent_terminal_tail = "",
  });

  SessionManager manager(&session_store, vibe::session::CreatePlatformPtyProcess,
                         std::chrono::milliseconds(1), std::chrono::milliseconds(1));

  EXPECT_EQ(manager.LoadPersistedSessions(), 0U);
  EXPECT_TRUE(manager.ListSessions().empty());
}

TEST(SessionManagerTest, CreateSessionReusesLowestAvailableSessionIdsAcrossRecoveredGaps) {
  FakeSessionStore session_store;
  session_store.sessions.push_back(vibe::store::PersistedSessionRecord{
      .session_id = "s_2",
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = "/tmp/recovered-two",
      .title = "recovered-two",
      .status = vibe::session::SessionStatus::Exited,
      .conversation_id = std::nullopt,
      .group_tags = {},
      .current_sequence = 2,
      .recent_terminal_tail = "tail-2",
  });
  session_store.sessions.push_back(vibe::store::PersistedSessionRecord{
      .session_id = "s_9",
      .provider = vibe::session::ProviderType::Claude,
      .workspace_root = "/tmp/recovered-nine",
      .title = "recovered-nine",
      .status = vibe::session::SessionStatus::Running,
      .conversation_id = std::nullopt,
      .group_tags = {},
      .current_sequence = 9,
      .recent_terminal_tail = "tail-9",
  });

  SessionManager manager(&session_store, vibe::session::CreatePlatformPtyProcess,
                         std::chrono::milliseconds(1), std::chrono::milliseconds(1));
  EXPECT_EQ(manager.LoadPersistedSessions(), 2U);

  const auto first_created = manager.CreateSession(CreateSessionRequest{
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = ".",
      .title = "live-one",
      .conversation_id = std::nullopt,
      .command_argv = std::vector<std::string>{"/bin/sh", "-c", "sleep 30"},
      .command_shell = std::nullopt,
      .group_tags = {},
  });
  ASSERT_TRUE(first_created.has_value());
  EXPECT_EQ(first_created->id.value(), "s_1");
  EXPECT_FALSE(first_created->is_recovered);
  EXPECT_TRUE(first_created->is_active);
  EXPECT_TRUE(first_created->created_at_unix_ms.has_value());
  EXPECT_TRUE(first_created->last_status_at_unix_ms.has_value());

  const auto second_created = manager.CreateSession(CreateSessionRequest{
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = ".",
      .title = "live-two",
      .conversation_id = std::nullopt,
      .command_argv = std::vector<std::string>{"/bin/sh", "-c", "sleep 30"},
      .command_shell = std::nullopt,
      .group_tags = {},
  });
  ASSERT_TRUE(second_created.has_value());
  EXPECT_EQ(second_created->id.value(), "s_3");
  EXPECT_FALSE(second_created->is_recovered);
  EXPECT_TRUE(second_created->is_active);

  const auto sessions = manager.ListSessions();
  ASSERT_EQ(sessions.size(), 4U);
  EXPECT_EQ(sessions[0].id.value(), "s_2");
  EXPECT_EQ(sessions[1].id.value(), "s_9");
  EXPECT_EQ(sessions[2].id.value(), "s_1");
  EXPECT_EQ(sessions[3].id.value(), "s_3");
}

TEST(SessionManagerTest, CreateSessionNormalizesAndPersistsGroupTags) {
  FakeSessionStore session_store;
  SessionManager manager(&session_store, vibe::session::CreatePlatformPtyProcess,
                         std::chrono::milliseconds(1), std::chrono::milliseconds(1));

  const auto created = manager.CreateSession(CreateSessionRequest{
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = ".",
      .title = "tagged",
      .conversation_id = std::nullopt,
      .command_argv = std::nullopt,
      .command_shell = std::string("sleep 30"),
      .group_tags = {" Frontend ", "mvp", "frontend"},
  });
  ASSERT_TRUE(created.has_value());
  EXPECT_EQ(created->group_tags, (std::vector<std::string>{"frontend", "mvp"}));

  const auto snapshot = manager.GetSnapshot(created->id.value());
  ASSERT_TRUE(snapshot.has_value());
  EXPECT_EQ(snapshot->metadata.group_tags, (std::vector<std::string>{"frontend", "mvp"}));

  const auto persisted = FindLastPersistedRecord(session_store, created->id.value());
  ASSERT_TRUE(persisted.has_value());
  EXPECT_EQ(persisted->group_tags, (std::vector<std::string>{"frontend", "mvp"}));
}

TEST(SessionManagerTest, CreateSessionSupportsShellCommandLaunchOverride) {
  FakeSessionStore session_store;
  SessionManager manager(&session_store, vibe::session::CreatePlatformPtyProcess,
                         std::chrono::milliseconds(1), std::chrono::milliseconds(1));

  const auto created = manager.CreateSession(CreateSessionRequest{
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = ".",
      .title = "shell-command",
      .conversation_id = std::nullopt,
      .command_argv = std::nullopt,
      .command_shell = std::string("printf 'shell-ready\\n'; sleep 30"),
      .group_tags = {},
  });
  ASSERT_TRUE(created.has_value());
  EXPECT_EQ(created->title, "shell-command");
}

TEST(SessionManagerTest, UpdatesGroupTagsForLiveAndRecoveredSessions) {
  FakeSessionStore session_store;
  session_store.sessions.push_back(vibe::store::PersistedSessionRecord{
      .session_id = "s_42",
      .provider = vibe::session::ProviderType::Claude,
      .workspace_root = "/tmp/recovered",
      .title = "recovered-session",
      .status = vibe::session::SessionStatus::Exited,
      .conversation_id = std::nullopt,
      .group_tags = {"legacy"},
      .current_sequence = 7,
      .recent_terminal_tail = "restored tail",
  });

  SessionManager manager(&session_store);
  ASSERT_EQ(manager.LoadPersistedSessions(), 1U);

  const auto live = manager.CreateSession(CreateSessionRequest{
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = ".",
      .title = "live-tagged",
      .conversation_id = std::nullopt,
      .command_argv = std::nullopt,
      .command_shell = std::string("sleep 30"),
      .group_tags = {"frontend"},
  });
  ASSERT_TRUE(live.has_value());

  const auto updated_live = manager.UpdateSessionGroupTags(
      live->id.value(), SessionGroupTagsUpdateMode::Add, {" MVP ", "frontend"});
  ASSERT_TRUE(updated_live.has_value());
  EXPECT_EQ(updated_live->group_tags, (std::vector<std::string>{"frontend", "mvp"}));

  const auto removed_live = manager.UpdateSessionGroupTags(
      live->id.value(), SessionGroupTagsUpdateMode::Remove, {"frontend"});
  ASSERT_TRUE(removed_live.has_value());
  EXPECT_EQ(removed_live->group_tags, (std::vector<std::string>{"mvp"}));

  const auto updated_recovered = manager.UpdateSessionGroupTags(
      "s_42", SessionGroupTagsUpdateMode::Set, {" Ops ", "ops", "archive"});
  ASSERT_TRUE(updated_recovered.has_value());
  EXPECT_EQ(updated_recovered->group_tags, (std::vector<std::string>{"ops", "archive"}));

  const auto recovered_snapshot = manager.GetSnapshot("s_42");
  ASSERT_TRUE(recovered_snapshot.has_value());
  EXPECT_EQ(recovered_snapshot->metadata.group_tags, (std::vector<std::string>{"ops", "archive"}));

  const auto persisted_live = FindLastPersistedRecord(session_store, live->id.value());
  ASSERT_TRUE(persisted_live.has_value());
  EXPECT_EQ(persisted_live->group_tags, (std::vector<std::string>{"mvp"}));

  const auto persisted_recovered = FindLastPersistedRecord(session_store, "s_42");
  ASSERT_TRUE(persisted_recovered.has_value());
  EXPECT_EQ(persisted_recovered->group_tags, (std::vector<std::string>{"ops", "archive"}));
}

TEST(SessionManagerTest, CreateSessionFailsWhenPtyFactoryCannotProvideProcess) {
  SessionManager manager(nullptr, []() -> std::unique_ptr<vibe::session::IPtyProcess> {
    return nullptr;
  });

  const auto created = manager.CreateSession(CreateSessionRequest{
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = ".",
      .title = "missing-pty",
      .conversation_id = std::nullopt,
      .command_argv = std::nullopt,
      .command_shell = std::nullopt,
      .group_tags = {},
  });

  EXPECT_FALSE(created.has_value());
  EXPECT_EQ(manager.last_create_error_message(), "pty process unavailable");
}

TEST(SessionManagerTest, CreateSessionSurfacesPtyStartFailureDetail) {
  SessionManager manager(nullptr, []() -> std::unique_ptr<vibe::session::IPtyProcess> {
    class FailingStartPtyProcess final : public vibe::session::IPtyProcess {
     public:
      [[nodiscard]] auto Start(const vibe::session::LaunchSpec&) -> vibe::session::StartResult override {
        return {.started = false, .pid = 0, .error_message = "execvp claude: No such file or directory"};
      }

      [[nodiscard]] auto Write(std::string_view) -> bool override { return false; }
      [[nodiscard]] auto Read(int) -> vibe::session::ReadResult override { return {}; }
      [[nodiscard]] auto ReadableFd() const -> std::optional<int> override { return std::nullopt; }
      [[nodiscard]] auto Resize(vibe::session::TerminalSize) -> bool override { return false; }
      [[nodiscard]] auto PollExit() -> std::optional<int> override { return std::nullopt; }
      [[nodiscard]] auto Terminate() -> bool override { return false; }
    };

    return std::make_unique<FailingStartPtyProcess>();
  });

  const auto created = manager.CreateSession(CreateSessionRequest{
      .provider = vibe::session::ProviderType::Claude,
      .workspace_root = ".",
      .title = "missing-claude",
      .conversation_id = std::nullopt,
      .command_argv = std::vector<std::string>{"claude"},
      .command_shell = std::nullopt,
      .group_tags = {},
  });

  EXPECT_FALSE(created.has_value());
  EXPECT_EQ(manager.last_create_error_message(), "execvp claude: No such file or directory");
  EXPECT_TRUE(manager.ListSessions().empty());
}

TEST(SessionManagerTest, CreateSessionWithDirectCommandDefaultsToBootstrapEnvironment) {
  std::optional<vibe::session::LaunchSpec> captured_launch_spec;
  SessionManager manager(nullptr, [&captured_launch_spec]() -> std::unique_ptr<vibe::session::IPtyProcess> {
    class CapturingPtyProcess final : public vibe::session::IPtyProcess {
     public:
      explicit CapturingPtyProcess(std::optional<vibe::session::LaunchSpec>* captured_launch_spec)
          : captured_launch_spec_(captured_launch_spec) {}

      [[nodiscard]] auto Start(const vibe::session::LaunchSpec& launch_spec) -> vibe::session::StartResult override {
        *captured_launch_spec_ = launch_spec;
        return {.started = false, .pid = 0, .error_message = "capture complete"};
      }

      [[nodiscard]] auto Write(std::string_view) -> bool override { return false; }
      [[nodiscard]] auto Read(int) -> vibe::session::ReadResult override { return {}; }
      [[nodiscard]] auto ReadableFd() const -> std::optional<int> override { return std::nullopt; }
      [[nodiscard]] auto Resize(vibe::session::TerminalSize) -> bool override { return false; }
      [[nodiscard]] auto PollExit() -> std::optional<int> override { return std::nullopt; }
      [[nodiscard]] auto Terminate() -> bool override { return false; }

     private:
      std::optional<vibe::session::LaunchSpec>* captured_launch_spec_{nullptr};
    };

    return std::make_unique<CapturingPtyProcess>(&captured_launch_spec);
  });
  const auto created = manager.CreateSession(CreateSessionRequest{
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = ".",
      .title = "direct-command",
      .conversation_id = std::nullopt,
      .command_argv = std::vector<std::string>{"/bin/sh", "-c", "sleep 30"},
      .command_shell = std::nullopt,
      .group_tags = {},
  });

  EXPECT_FALSE(created.has_value());
  ASSERT_TRUE(captured_launch_spec.has_value());
  EXPECT_EQ(captured_launch_spec->effective_environment.mode,
            vibe::session::EnvMode::BootstrapFromShell);
}

TEST(SessionManagerTest, CreateSessionWithGarbageCommandSurfacesLaunchFailureDetail) {
  SessionManager manager(nullptr, vibe::session::CreatePlatformPtyProcess,
                         std::chrono::milliseconds(1), std::chrono::milliseconds(1));

  const auto created = manager.CreateSession(CreateSessionRequest{
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = ".",
      .title = "garbage-command",
      .conversation_id = std::nullopt,
      .command_argv = std::vector<std::string>{"__sentrits_missing_command__"},
      .command_shell = std::nullopt,
      .group_tags = {},
  });

  EXPECT_FALSE(created.has_value());
  EXPECT_NE(manager.last_create_error_message().find("No such file or directory"), std::string::npos);
  EXPECT_TRUE(manager.ListSessions().empty());
}

TEST(SessionManagerTest, CreateSessionCanStartThenExitImmediately) {
  FakeSessionStore session_store;
  SessionManager manager(&session_store, vibe::session::CreatePlatformPtyProcess,
                         std::chrono::milliseconds(1), std::chrono::milliseconds(1));

  const auto created = manager.CreateSession(CreateSessionRequest{
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = ".",
      .title = "exit-immediately",
      .conversation_id = std::nullopt,
      .command_argv = std::vector<std::string>{"/bin/sh", "-c", "printf 'bye\\n'; exit 0"},
      .command_shell = std::nullopt,
      .group_tags = {},
  });
  ASSERT_TRUE(created.has_value());
  EXPECT_TRUE(manager.last_create_error_message().empty());

  const auto summary = PollUntilStatus(manager, created->id.value(), vibe::session::SessionStatus::Exited);
  ASSERT_TRUE(summary.has_value());
  EXPECT_EQ(summary->status, vibe::session::SessionStatus::Exited);
  EXPECT_TRUE(summary->last_output_at_unix_ms.has_value());
  EXPECT_GT(summary->current_sequence, 0U);

  const auto tail = manager.GetTail(created->id.value(), 64);
  ASSERT_TRUE(tail.has_value());
  EXPECT_NE(tail->data.find("bye"), std::string::npos);
}

TEST(SessionManagerTest, ShutdownTerminatesLiveSessionsClearsControlAndPersistsExitedState) {
  FakeSessionStore session_store;
  SessionManager manager(&session_store, vibe::session::CreatePlatformPtyProcess,
                         std::chrono::milliseconds(1), std::chrono::milliseconds(1));

  const auto created = manager.CreateSession(CreateSessionRequest{
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = ".",
      .title = "shutdown-target",
      .conversation_id = std::nullopt,
      .command_argv = std::vector<std::string>{"/bin/sh", "-c", "sleep 30"},
      .command_shell = std::nullopt,
      .group_tags = {},
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

TEST(SessionManagerTest, ClearInactiveSessionsRemovesExitedAndRecoveredRecords) {
  FakeSessionStore session_store;
  session_store.sessions.push_back(vibe::store::PersistedSessionRecord{
      .session_id = "s_2",
      .provider = vibe::session::ProviderType::Claude,
      .workspace_root = "/tmp/recovered",
      .title = "recovered-session",
      .status = vibe::session::SessionStatus::Exited,
      .conversation_id = std::nullopt,
      .group_tags = {},
      .current_sequence = 7,
      .recent_terminal_tail = "restored tail",
  });

  SessionManager manager(&session_store);
  EXPECT_EQ(manager.LoadPersistedSessions(), 1U);

  const auto created = manager.CreateSession(CreateSessionRequest{
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = ".",
      .title = "clear-target",
      .conversation_id = std::nullopt,
      .command_argv = std::vector<std::string>{"/bin/sh", "-c", "sleep 30"},
      .command_shell = std::nullopt,
      .group_tags = {},
  });
  ASSERT_TRUE(created.has_value());

  EXPECT_TRUE(manager.StopSession(created->id.value()));

  EXPECT_EQ(manager.ClearInactiveSessions(), 2U);
  EXPECT_TRUE(manager.ListSessions().empty());
  EXPECT_EQ(session_store.removed.size(), 2U);
  EXPECT_EQ(session_store.removed[0], "s_2");
  EXPECT_EQ(session_store.removed[1], created->id.value());
}

TEST(SessionManagerTest, CreateSessionReusesLowestAvailableSessionIdAfterCleanup) {
  FakeSessionStore session_store;
  SessionManager manager(&session_store);

  const auto first = manager.CreateSession(CreateSessionRequest{
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = ".",
      .title = "first",
      .conversation_id = std::nullopt,
      .command_argv = std::vector<std::string>{"/bin/sh", "-c", "sleep 30"},
      .command_shell = std::nullopt,
      .group_tags = {},
  });
  const auto second = manager.CreateSession(CreateSessionRequest{
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = ".",
      .title = "second",
      .conversation_id = std::nullopt,
      .command_argv = std::vector<std::string>{"/bin/sh", "-c", "sleep 30"},
      .command_shell = std::nullopt,
      .group_tags = {},
  });
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(first->id.value(), "s_1");
  EXPECT_EQ(second->id.value(), "s_2");

  ASSERT_TRUE(manager.StopSession(first->id.value()));
  ASSERT_TRUE(manager.StopSession(second->id.value()));
  EXPECT_EQ(manager.ClearInactiveSessions(), 2U);
  EXPECT_TRUE(manager.ListSessions().empty());

  const auto reused = manager.CreateSession(CreateSessionRequest{
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = ".",
      .title = "reused",
      .conversation_id = std::nullopt,
      .command_argv = std::vector<std::string>{"/bin/sh", "-c", "sleep 30"},
      .command_shell = std::nullopt,
      .group_tags = {},
  });
  ASSERT_TRUE(reused.has_value());
  EXPECT_EQ(reused->id.value(), "s_1");
}

TEST(SessionManagerTest, PollAllUpdatesOutputAndActivityTimestampsForLiveSession) {
  FakeSessionStore session_store;
  SessionManager manager(&session_store);

  const auto created = manager.CreateSession(CreateSessionRequest{
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = ".",
      .title = "output-target",
      .conversation_id = std::nullopt,
      .command_argv = std::vector<std::string>{"/bin/sh", "-c", "printf 'ready\\n'; sleep 1"},
      .command_shell = std::nullopt,
      .group_tags = {},
  });
  ASSERT_TRUE(created.has_value());

  manager.PollAll(100);

  const auto summary = manager.GetSession(created->id.value());
  ASSERT_TRUE(summary.has_value());
  EXPECT_TRUE(summary->last_output_at_unix_ms.has_value());
  EXPECT_TRUE(summary->last_activity_at_unix_ms.has_value());
  EXPECT_GT(summary->current_sequence, 0U);
}

TEST(SessionManagerTest, ControlHandoffUpdatesActivityTimestamp) {
  FakeSessionStore session_store;
  SessionManager manager(&session_store);

  const auto created = manager.CreateSession(CreateSessionRequest{
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = ".",
      .title = "control-activity",
      .conversation_id = std::nullopt,
      .command_argv = std::vector<std::string>{"/bin/sh", "-c", "sleep 30"},
      .command_shell = std::nullopt,
      .group_tags = {},
  });
  ASSERT_TRUE(created.has_value());

  const auto initial_summary = manager.GetSession(created->id.value());
  ASSERT_TRUE(initial_summary.has_value());
  ASSERT_TRUE(initial_summary->last_activity_at_unix_ms.has_value());
  const auto activity_before = *initial_summary->last_activity_at_unix_ms;

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  ASSERT_TRUE(manager.RequestControl(created->id.value(), "remote-1",
                                     vibe::session::ControllerKind::Remote));

  const auto requested_summary = manager.GetSession(created->id.value());
  ASSERT_TRUE(requested_summary.has_value());
  ASSERT_TRUE(requested_summary->last_activity_at_unix_ms.has_value());
  EXPECT_GT(*requested_summary->last_activity_at_unix_ms, activity_before);
  EXPECT_EQ(requested_summary->attention_state, vibe::session::AttentionState::Info);
  EXPECT_EQ(requested_summary->attention_reason, vibe::session::AttentionReason::ControllerChanged);
  EXPECT_TRUE(requested_summary->last_controller_change_at_unix_ms.has_value());
  EXPECT_EQ(requested_summary->attention_since_unix_ms, requested_summary->last_controller_change_at_unix_ms);

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  ASSERT_TRUE(manager.ReleaseControl(created->id.value(), "remote-1"));

  const auto released_summary = manager.GetSession(created->id.value());
  ASSERT_TRUE(released_summary.has_value());
  ASSERT_TRUE(released_summary->last_activity_at_unix_ms.has_value());
  EXPECT_GT(*released_summary->last_activity_at_unix_ms, *requested_summary->last_activity_at_unix_ms);

  EXPECT_EQ(manager.Shutdown(), 1U);
}

TEST_F(GitSessionManagerTest, GitPollDoesNotAdvanceActivityWithoutGitStateChange) {
  FakeSessionStore session_store;
  SessionManager manager(&session_store, vibe::session::CreatePlatformPtyProcess,
                         std::chrono::milliseconds(1), std::chrono::milliseconds(1));

  const auto created = manager.CreateSession(CreateSessionRequest{
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = test_dir_.string(),
      .title = "git-idle",
      .conversation_id = std::nullopt,
      .command_argv = std::vector<std::string>{"/bin/sh", "-c", "sleep 30"},
      .command_shell = std::nullopt,
      .group_tags = {},
  });
  ASSERT_TRUE(created.has_value());

  PollUntilGitCheck(manager);
  const auto initial_summary = manager.GetSession(created->id.value());
  ASSERT_TRUE(initial_summary.has_value());
  ASSERT_TRUE(initial_summary->last_activity_at_unix_ms.has_value());
  const auto activity_before = *initial_summary->last_activity_at_unix_ms;

  PollUntilGitCheck(manager);
  const auto later_summary = manager.GetSession(created->id.value());
  ASSERT_TRUE(later_summary.has_value());
  ASSERT_TRUE(later_summary->last_activity_at_unix_ms.has_value());
  EXPECT_EQ(*later_summary->last_activity_at_unix_ms, activity_before);
  EXPECT_FALSE(later_summary->git_dirty);
  EXPECT_FALSE(later_summary->git_branch.empty());
  EXPECT_EQ(later_summary->git_modified_count, 0U);
  EXPECT_EQ(later_summary->git_staged_count, 0U);
  EXPECT_EQ(later_summary->git_untracked_count, 0U);
}

TEST_F(GitSessionManagerTest, GitPollTracksDirtyAndCleanTransitionsInSummaryAndSnapshot) {
  FakeSessionStore session_store;
  SessionManager manager(&session_store, vibe::session::CreatePlatformPtyProcess,
                         std::chrono::milliseconds(1), std::chrono::milliseconds(1));

  const auto created = manager.CreateSession(CreateSessionRequest{
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = test_dir_.string(),
      .title = "git-transitions",
      .conversation_id = std::nullopt,
      .command_argv = std::vector<std::string>{"/bin/sh", "-c", "sleep 30"},
      .command_shell = std::nullopt,
      .group_tags = {},
  });
  ASSERT_TRUE(created.has_value());

  PollUntilGitCheck(manager);
  const auto clean_summary = manager.GetSession(created->id.value());
  ASSERT_TRUE(clean_summary.has_value());
  ASSERT_TRUE(clean_summary->last_activity_at_unix_ms.has_value());

  WriteFile("tracked.txt", "changed\n");
  WriteFile("new.txt", "new\n");
  PollUntilGitCheck(manager);

  const auto dirty_summary = manager.GetSession(created->id.value());
  ASSERT_TRUE(dirty_summary.has_value());
  EXPECT_TRUE(dirty_summary->git_dirty);
  EXPECT_FALSE(dirty_summary->git_branch.empty());
  EXPECT_EQ(dirty_summary->git_modified_count, 1U);
  EXPECT_EQ(dirty_summary->git_staged_count, 0U);
  EXPECT_EQ(dirty_summary->git_untracked_count, 1U);
  EXPECT_EQ(dirty_summary->attention_state, vibe::session::AttentionState::Info);
  EXPECT_EQ(dirty_summary->attention_reason, vibe::session::AttentionReason::WorkspaceChanged);
  EXPECT_TRUE(dirty_summary->last_git_change_at_unix_ms.has_value());
  ASSERT_TRUE(dirty_summary->last_activity_at_unix_ms.has_value());
  EXPECT_GT(*dirty_summary->last_activity_at_unix_ms, *clean_summary->last_activity_at_unix_ms);

  const auto dirty_snapshot = manager.GetSnapshot(created->id.value());
  ASSERT_TRUE(dirty_snapshot.has_value());
  EXPECT_TRUE(dirty_snapshot->signals.git_dirty);
  EXPECT_EQ(dirty_snapshot->signals.git_modified_count, 1U);
  EXPECT_EQ(dirty_snapshot->signals.git_staged_count, 0U);
  EXPECT_EQ(dirty_snapshot->signals.git_untracked_count, 1U);
  EXPECT_EQ(dirty_snapshot->git_summary.modified_count, 1U);
  EXPECT_EQ(dirty_snapshot->git_summary.staged_count, 0U);
  EXPECT_EQ(dirty_snapshot->git_summary.untracked_count, 1U);
  EXPECT_EQ(dirty_snapshot->git_summary.modified_files, (std::vector<std::string>{"tracked.txt"}));
  EXPECT_EQ(dirty_snapshot->git_summary.untracked_files, (std::vector<std::string>{"new.txt"}));

  RunGit("add tracked.txt new.txt");
  RunGit("commit -m \"clean repo\"");
  PollUntilGitCheck(manager);

  const auto clean_again = manager.GetSession(created->id.value());
  ASSERT_TRUE(clean_again.has_value());
  EXPECT_FALSE(clean_again->git_dirty);
  EXPECT_EQ(clean_again->git_modified_count, 0U);
  EXPECT_EQ(clean_again->git_staged_count, 0U);
  EXPECT_EQ(clean_again->git_untracked_count, 0U);

  const auto clean_snapshot = manager.GetSnapshot(created->id.value());
  ASSERT_TRUE(clean_snapshot.has_value());
  EXPECT_FALSE(clean_snapshot->signals.git_dirty);
  EXPECT_EQ(clean_snapshot->signals.git_modified_count, 0U);
  EXPECT_EQ(clean_snapshot->signals.git_staged_count, 0U);
  EXPECT_EQ(clean_snapshot->signals.git_untracked_count, 0U);
  EXPECT_EQ(clean_snapshot->git_summary.modified_count, 0U);
  EXPECT_EQ(clean_snapshot->git_summary.staged_count, 0U);
  EXPECT_EQ(clean_snapshot->git_summary.untracked_count, 0U);
  EXPECT_TRUE(clean_snapshot->git_summary.modified_files.empty());
  EXPECT_TRUE(clean_snapshot->git_summary.staged_files.empty());
  EXPECT_TRUE(clean_snapshot->git_summary.untracked_files.empty());
}

TEST(SessionManagerTest, InfersActiveQuietAndStoppedSupervisionStatesConservatively) {
  using vibe::session::SessionStatus;
  using vibe::session::SupervisionState;

  EXPECT_EQ(InferSupervisionState(SessionStatus::Running, 1'000, 5'500), SupervisionState::Active);
  EXPECT_EQ(InferSupervisionState(SessionStatus::AwaitingInput, 1'000, 8'000), SupervisionState::Quiet);
  EXPECT_EQ(InferSupervisionState(SessionStatus::Starting, std::nullopt, 8'000), SupervisionState::Quiet);
  EXPECT_EQ(InferSupervisionState(SessionStatus::Exited, 1'000, 1'001), SupervisionState::Stopped);
  EXPECT_EQ(InferSupervisionState(SessionStatus::Error, std::nullopt, 1'001), SupervisionState::Stopped);
}

TEST(SessionManagerTest, StopSetsShortLivedExitedAttention) {
  FakeSessionStore session_store;
  SessionManager manager(&session_store, vibe::session::CreatePlatformPtyProcess,
                         std::chrono::milliseconds(1), std::chrono::milliseconds(1));

  const auto created = manager.CreateSession(CreateSessionRequest{
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = ".",
      .title = "exit-attention",
      .conversation_id = std::nullopt,
      .command_argv = std::vector<std::string>{"/bin/sh", "-c", "sleep 30"},
      .command_shell = std::nullopt,
      .group_tags = {},
  });
  ASSERT_TRUE(created.has_value());

  ASSERT_TRUE(manager.StopSession(created->id.value()));

  const auto summary = manager.GetSession(created->id.value());
  ASSERT_TRUE(summary.has_value());
  EXPECT_EQ(summary->status, vibe::session::SessionStatus::Exited);
  EXPECT_EQ(summary->attention_state, vibe::session::AttentionState::Info);
  EXPECT_EQ(summary->attention_reason, vibe::session::AttentionReason::SessionExitedCleanly);
  EXPECT_EQ(summary->attention_since_unix_ms, summary->last_status_at_unix_ms);
}

TEST(SessionManagerTest, ViewportResizeDoesNotChangeSessionPtySize) {
  FakeSessionStore session_store;
  SessionManager manager(&session_store);

  const auto created = manager.CreateSession(CreateSessionRequest{
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = ".",
      .title = "viewport-only",
      .conversation_id = std::nullopt,
      .command_argv = std::vector<std::string>{"/bin/sh", "-c", "sleep 30"},
      .command_shell = std::nullopt,
      .group_tags = {},
  });
  ASSERT_TRUE(created.has_value());

  const auto before = manager.GetSession(created->id.value());
  ASSERT_TRUE(before.has_value());
  ASSERT_TRUE(before->pty_columns.has_value());
  ASSERT_TRUE(before->pty_rows.has_value());

  EXPECT_TRUE(manager.UpdateViewport(created->id.value(), "observer-1",
                                     vibe::session::TerminalSize{.columns = 40, .rows = 12}));
  const auto viewport = manager.GetViewportSnapshot(created->id.value(), "observer-1");
  ASSERT_TRUE(viewport.has_value());
  EXPECT_EQ(viewport->columns, 40);
  EXPECT_EQ(viewport->rows, 12);

  const auto after = manager.GetSession(created->id.value());
  ASSERT_TRUE(after.has_value());
  EXPECT_EQ(after->pty_columns, before->pty_columns);
  EXPECT_EQ(after->pty_rows, before->pty_rows);

  manager.RemoveViewport(created->id.value(), "observer-1");
  EXPECT_FALSE(manager.GetViewportSnapshot(created->id.value(), "observer-1").has_value());
  EXPECT_EQ(manager.Shutdown(), 1U);
}

TEST(SessionManagerTest, ReadFileReturnsContentWithinRecoveredWorkspaceRoot) {
  const auto temp_root =
      std::filesystem::temp_directory_path() /
      ("vibe-session-file-read-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(temp_root / "src");

  {
    std::ofstream file(temp_root / "src" / "main.cpp");
    file << "int main() { return 0; }\n";
  }

  FakeSessionStore session_store;
  session_store.sessions.push_back(vibe::store::PersistedSessionRecord{
      .session_id = "s_42",
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = temp_root.string(),
      .title = "recovered-session",
      .status = vibe::session::SessionStatus::Exited,
      .conversation_id = std::nullopt,
      .group_tags = {},
      .current_sequence = 7,
      .recent_terminal_tail = "restored tail",
  });

  SessionManager manager(&session_store);
  ASSERT_EQ(manager.LoadPersistedSessions(), 1U);

  const auto file = manager.ReadFile("s_42", "src/main.cpp", 1024);
  EXPECT_EQ(file.status, FileReadStatus::Ok);
  EXPECT_EQ(file.workspace_path, "src/main.cpp");
  EXPECT_EQ(file.size_bytes, 25U);
  EXPECT_FALSE(file.truncated);
  EXPECT_EQ(file.content, "int main() { return 0; }\n");

  std::filesystem::remove_all(temp_root);
}

TEST(SessionManagerTest, ReadFileRejectsInvalidOrEscapingPaths) {
  const auto temp_root =
      std::filesystem::temp_directory_path() /
      ("vibe-session-file-path-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(temp_root / "src");

  {
    std::ofstream file(temp_root / "src" / "main.cpp");
    file << "content\n";
  }

  FakeSessionStore session_store;
  session_store.sessions.push_back(vibe::store::PersistedSessionRecord{
      .session_id = "s_42",
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = temp_root.string(),
      .title = "recovered-session",
      .status = vibe::session::SessionStatus::Exited,
      .conversation_id = std::nullopt,
      .group_tags = {},
      .current_sequence = 7,
      .recent_terminal_tail = "restored tail",
  });

  SessionManager manager(&session_store);
  ASSERT_EQ(manager.LoadPersistedSessions(), 1U);

  EXPECT_EQ(manager.ReadFile("missing", "src/main.cpp", 1024).status, FileReadStatus::SessionNotFound);
  EXPECT_EQ(manager.ReadFile("s_42", "", 1024).status, FileReadStatus::InvalidPath);
  EXPECT_EQ(manager.ReadFile("s_42", "/etc/passwd", 1024).status, FileReadStatus::InvalidPath);
  EXPECT_EQ(manager.ReadFile("s_42", "../outside.txt", 1024).status, FileReadStatus::InvalidPath);
  EXPECT_EQ(manager.ReadFile("s_42", "src/missing.cpp", 1024).status, FileReadStatus::NotFound);
  EXPECT_EQ(manager.ReadFile("s_42", "src", 1024).status, FileReadStatus::NotRegularFile);

  std::filesystem::remove_all(temp_root);
}

TEST(SessionManagerTest, ReadFileMarksTruncatedResponses) {
  const auto temp_root =
      std::filesystem::temp_directory_path() /
      ("vibe-session-file-truncate-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(temp_root);

  {
    std::ofstream file(temp_root / "notes.txt");
    file << "abcdef";
  }

  FakeSessionStore session_store;
  session_store.sessions.push_back(vibe::store::PersistedSessionRecord{
      .session_id = "s_42",
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = temp_root.string(),
      .title = "recovered-session",
      .status = vibe::session::SessionStatus::Exited,
      .conversation_id = std::nullopt,
      .group_tags = {},
      .current_sequence = 7,
      .recent_terminal_tail = "restored tail",
  });

  SessionManager manager(&session_store);
  ASSERT_EQ(manager.LoadPersistedSessions(), 1U);

  const auto file = manager.ReadFile("s_42", "notes.txt", 4);
  EXPECT_EQ(file.status, FileReadStatus::Ok);
  EXPECT_EQ(file.size_bytes, 6U);
  EXPECT_TRUE(file.truncated);
  EXPECT_EQ(file.content, "abcd");

  std::filesystem::remove_all(temp_root);
}

TEST(SessionManagerTest, PollAllTracksRecentWorkspaceFileChangesForLiveSession) {
  const auto temp_root =
      std::filesystem::temp_directory_path() /
      ("vibe-session-live-file-watch-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(temp_root / "src");
  {
    std::ofstream file(temp_root / "src" / "main.cpp");
    file << "int main() { return 0; }\n";
  }

  FakeSessionStore session_store;
  SessionManager manager(&session_store, vibe::session::CreatePlatformPtyProcess,
                         std::chrono::milliseconds(1), std::chrono::milliseconds(1));
  const auto created = manager.CreateSession(CreateSessionRequest{
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = temp_root.string(),
      .title = "file-watch",
      .conversation_id = std::nullopt,
      .command_argv = std::vector<std::string>{"/bin/sh", "-c", "sleep 30"},
      .command_shell = std::nullopt,
      .group_tags = {},
  });
  ASSERT_TRUE(created.has_value());

  for (int index = 0; index < 20; ++index) {
    manager.PollAll(0);
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  {
    std::ofstream file(temp_root / "src" / "main.cpp");
    file << "int main() { return 1; }\n";
  }
  {
    std::ofstream file(temp_root / "notes.txt");
    file << "notes\n";
  }

  std::optional<vibe::session::SessionSnapshot> snapshot;
  for (int attempt = 0; attempt < 20; ++attempt) {
    manager.PollAll(0);
    snapshot = manager.GetSnapshot(created->id.value());
    if (snapshot.has_value() && snapshot->recent_file_changes ==
                                    std::vector<std::string>{"notes.txt", "src/main.cpp"}) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  ASSERT_TRUE(snapshot.has_value());
  EXPECT_EQ(snapshot->recent_file_changes, (std::vector<std::string>{"notes.txt", "src/main.cpp"}));
  EXPECT_EQ(snapshot->signals.recent_file_change_count, 2U);
  EXPECT_EQ(snapshot->signals.attention_state, vibe::session::AttentionState::Info);
  EXPECT_EQ(snapshot->signals.attention_reason, vibe::session::AttentionReason::WorkspaceChanged);
  EXPECT_TRUE(snapshot->signals.last_file_change_at_unix_ms.has_value());
  EXPECT_EQ(snapshot->signals.attention_since_unix_ms, snapshot->signals.last_file_change_at_unix_ms);

  const auto summary = manager.GetSession(created->id.value());
  ASSERT_TRUE(summary.has_value());
  EXPECT_EQ(summary->recent_file_change_count, 2U);
  EXPECT_EQ(summary->attention_state, vibe::session::AttentionState::Info);
  EXPECT_EQ(summary->attention_reason, vibe::session::AttentionReason::WorkspaceChanged);

  EXPECT_EQ(manager.Shutdown(), 1U);
  std::filesystem::remove_all(temp_root);
}

}  // namespace
}  // namespace vibe::service
