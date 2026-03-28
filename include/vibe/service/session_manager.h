#ifndef VIBE_SERVICE_SESSION_MANAGER_H
#define VIBE_SERVICE_SESSION_MANAGER_H

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "vibe/service/git_inspector.h"
#include "vibe/service/workspace_file_watcher.h"
#include "vibe/session/launch_spec.h"
#include "vibe/session/pty_process.h"
#include "vibe/session/pty_process_factory.h"
#include "vibe/session/session_runtime.h"
#include "vibe/session/session_snapshot.h"
#include "vibe/session/session_types.h"
#include "vibe/store/session_store.h"

namespace vibe::service {

struct CreateSessionRequest {
  vibe::session::ProviderType provider;
  std::string workspace_root;
  std::string title;
  std::optional<std::string> conversation_id;
  std::optional<std::vector<std::string>> command_argv;
  std::vector<std::string> group_tags;
};

enum class SessionGroupTagsUpdateMode {
  Add,
  Remove,
  Set,
};

struct SessionSummary {
  vibe::session::SessionId id;
  vibe::session::ProviderType provider;
  std::string workspace_root;
  std::string title;
  vibe::session::SessionStatus status;
  std::optional<std::string> conversation_id;
  std::vector<std::string> group_tags;
  std::optional<std::string> controller_client_id;
  vibe::session::ControllerKind controller_kind{vibe::session::ControllerKind::None};
  bool is_recovered{false};
  bool is_active{false};
  vibe::session::SupervisionState supervision_state{vibe::session::SupervisionState::Quiet};
  vibe::session::AttentionState attention_state{vibe::session::AttentionState::None};
  vibe::session::AttentionReason attention_reason{vibe::session::AttentionReason::None};
  std::optional<std::int64_t> created_at_unix_ms;
  std::optional<std::int64_t> last_status_at_unix_ms;
  std::optional<std::int64_t> last_output_at_unix_ms;
  std::optional<std::int64_t> last_activity_at_unix_ms;
  std::optional<std::int64_t> last_file_change_at_unix_ms;
  std::optional<std::int64_t> last_git_change_at_unix_ms;
  std::optional<std::int64_t> last_controller_change_at_unix_ms;
  std::optional<std::int64_t> attention_since_unix_ms;
  std::uint64_t current_sequence{0};
  std::size_t attached_client_count{0};
  std::size_t recent_file_change_count{0};
  bool git_dirty{false};
  std::string git_branch;
  std::size_t git_modified_count{0};
  std::size_t git_staged_count{0};
  std::size_t git_untracked_count{0};
};

[[nodiscard]] auto InferSupervisionState(vibe::session::SessionStatus status,
                                         std::optional<std::int64_t> last_output_at_unix_ms,
                                         std::int64_t now_unix_ms)
    -> vibe::session::SupervisionState;

enum class FileReadStatus {
  Ok,
  SessionNotFound,
  InvalidPath,
  WorkspaceUnavailable,
  NotFound,
  NotRegularFile,
  IoError,
};

struct SessionFileReadResult {
  FileReadStatus status{FileReadStatus::SessionNotFound};
  std::string workspace_path;
  std::string content;
  std::uint64_t size_bytes{0};
  bool truncated{false};
};

class SessionManager {
 public:
  using PtyProcessFactory = std::function<std::unique_ptr<vibe::session::IPtyProcess>()>;

  explicit SessionManager(vibe::store::SessionStore* session_store = nullptr,
                          PtyProcessFactory pty_process_factory =
                              vibe::session::CreatePlatformPtyProcess);

  [[nodiscard]] auto CreateSession(const CreateSessionRequest& request)
      -> std::optional<SessionSummary>;
  [[nodiscard]] auto LoadPersistedSessions() -> std::size_t;
  [[nodiscard]] auto ListSessions() const -> std::vector<SessionSummary>;
  [[nodiscard]] auto GetSession(const std::string& session_id) const -> std::optional<SessionSummary>;
  [[nodiscard]] auto GetSnapshot(const std::string& session_id) const
      -> std::optional<vibe::session::SessionSnapshot>;
  [[nodiscard]] auto GetTail(const std::string& session_id, std::size_t bytes) const
      -> std::optional<vibe::session::OutputSlice>;
  [[nodiscard]] auto GetOutputSince(const std::string& session_id, std::uint64_t seq) const
      -> std::optional<vibe::session::OutputSlice>;
  [[nodiscard]] auto GetReadableFd(const std::string& session_id) const -> std::optional<int>;
  [[nodiscard]] auto ReadFile(const std::string& session_id, const std::string& workspace_path,
                              std::size_t max_bytes) const -> SessionFileReadResult;
  [[nodiscard]] auto SendInput(const std::string& session_id, const std::string& input) -> bool;
  [[nodiscard]] auto UpdateSessionGroupTags(const std::string& session_id,
                                            SessionGroupTagsUpdateMode mode,
                                            const std::vector<std::string>& tags)
      -> std::optional<SessionSummary>;
  [[nodiscard]] auto ResizeSession(const std::string& session_id,
                                   vibe::session::TerminalSize terminal_size) -> bool;
  [[nodiscard]] auto StopSession(const std::string& session_id) -> bool;
  [[nodiscard]] auto ClearInactiveSessions() -> std::size_t;
  [[nodiscard]] auto RequestControl(const std::string& session_id,
                                    const std::string& client_id,
                                    vibe::session::ControllerKind controller_kind) -> bool;
  [[nodiscard]] auto ReleaseControl(const std::string& session_id, const std::string& client_id) -> bool;
  [[nodiscard]] auto HasControl(const std::string& session_id, const std::string& client_id) const -> bool;
  [[nodiscard]] auto HasPrivilegedLocalController(const std::string& session_id) const -> bool;
  [[nodiscard]] auto Shutdown() -> std::size_t;
  [[nodiscard]] auto PollSession(const std::string& session_id, int read_timeout_ms) -> bool;
  void PollAll(int read_timeout_ms);

 private:
  struct SessionEntry {
    vibe::session::SessionId id;
    std::unique_ptr<vibe::session::IPtyProcess> process;
    std::unique_ptr<vibe::session::SessionRuntime> runtime;
    std::unique_ptr<vibe::service::GitInspector> git_inspector;
    std::unique_ptr<vibe::service::WorkspaceFileWatcher> file_watcher;
    std::optional<vibe::session::SessionSnapshot> recovered_snapshot;
    std::optional<std::string> controller_client_id;
    vibe::session::ControllerKind controller_kind{vibe::session::ControllerKind::None};
    bool is_recovered{false};
    std::optional<std::int64_t> created_at_unix_ms;
    std::optional<std::int64_t> last_status_at_unix_ms;
    std::optional<std::int64_t> last_output_at_unix_ms;
    std::optional<std::int64_t> last_activity_at_unix_ms;
    std::optional<std::int64_t> last_file_change_at_unix_ms;
    std::optional<std::int64_t> last_git_change_at_unix_ms;
    std::optional<std::int64_t> last_controller_change_at_unix_ms;
    vibe::session::SessionStatus last_observed_status{vibe::session::SessionStatus::Created};
    std::uint64_t last_observed_sequence{0};
  };

  [[nodiscard]] auto BuildSummary(const SessionEntry& entry) const -> SessionSummary;
  void ResetControllerState(SessionEntry& entry);
  void PersistEntry(const SessionEntry& entry);
  [[nodiscard]] auto MakeSessionId() const -> std::optional<vibe::session::SessionId>;
  [[nodiscard]] auto FindEntry(const std::string& session_id) -> SessionEntry*;
  [[nodiscard]] auto FindEntry(const std::string& session_id) const -> const SessionEntry*;

  vibe::store::SessionStore* session_store_{nullptr};
  PtyProcessFactory pty_process_factory_;
  std::vector<SessionEntry> sessions_;
  int poll_count_{0};
};

}  // namespace vibe::service

#endif
