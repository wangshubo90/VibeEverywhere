#ifndef VIBE_SERVICE_SESSION_MANAGER_H
#define VIBE_SERVICE_SESSION_MANAGER_H

#include <memory>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "vibe/service/git_inspector.h"
#include "vibe/session/launch_spec.h"
#include "vibe/session/posix_pty_process.h"
#include "vibe/session/session_runtime.h"
#include "vibe/session/session_snapshot.h"
#include "vibe/session/session_types.h"
#include "vibe/store/session_store.h"

namespace vibe::service {

struct CreateSessionRequest {
  vibe::session::ProviderType provider;
  std::string workspace_root;
  std::string title;
  std::optional<std::vector<std::string>> command_argv;
};

struct SessionSummary {
  vibe::session::SessionId id;
  vibe::session::ProviderType provider;
  std::string workspace_root;
  std::string title;
  vibe::session::SessionStatus status;
  std::optional<std::string> controller_client_id;
  vibe::session::ControllerKind controller_kind{vibe::session::ControllerKind::None};
  bool is_recovered{false};
  bool is_active{false};
  std::optional<std::int64_t> created_at_unix_ms;
  std::optional<std::int64_t> last_status_at_unix_ms;
};

class SessionManager {
 public:
  explicit SessionManager(vibe::store::SessionStore* session_store = nullptr);

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
  [[nodiscard]] auto SendInput(const std::string& session_id, const std::string& input) -> bool;
  [[nodiscard]] auto ResizeSession(const std::string& session_id,
                                   vibe::session::TerminalSize terminal_size) -> bool;
  [[nodiscard]] auto StopSession(const std::string& session_id) -> bool;
  [[nodiscard]] auto ClearInactiveSessions() -> std::size_t;
  [[nodiscard]] auto RequestControl(const std::string& session_id,
                                    const std::string& client_id,
                                    vibe::session::ControllerKind controller_kind) -> bool;
  [[nodiscard]] auto ReleaseControl(const std::string& session_id, const std::string& client_id) -> bool;
  [[nodiscard]] auto HasControl(const std::string& session_id, const std::string& client_id) const -> bool;
  [[nodiscard]] auto Shutdown() -> std::size_t;
  void PollAll(int read_timeout_ms);

 private:
  struct SessionEntry {
    vibe::session::SessionId id;
    std::unique_ptr<vibe::session::PosixPtyProcess> process;
    std::unique_ptr<vibe::session::SessionRuntime> runtime;
    std::unique_ptr<vibe::service::GitInspector> git_inspector;
    std::optional<vibe::session::SessionSnapshot> recovered_snapshot;
    std::optional<std::string> controller_client_id;
    vibe::session::ControllerKind controller_kind{vibe::session::ControllerKind::None};
    bool is_recovered{false};
    std::optional<std::int64_t> created_at_unix_ms;
    std::optional<std::int64_t> last_status_at_unix_ms;
    vibe::session::SessionStatus last_observed_status{vibe::session::SessionStatus::Created};
  };

  [[nodiscard]] auto BuildSummary(const SessionEntry& entry) const -> SessionSummary;
  void ResetControllerState(SessionEntry& entry);
  void PersistEntry(const SessionEntry& entry);
  [[nodiscard]] auto MakeSessionId() const -> std::optional<vibe::session::SessionId>;
  [[nodiscard]] auto FindEntry(const std::string& session_id) -> SessionEntry*;
  [[nodiscard]] auto FindEntry(const std::string& session_id) const -> const SessionEntry*;

  vibe::store::SessionStore* session_store_{nullptr};
  std::vector<SessionEntry> sessions_;
  int poll_count_{0};
};

}  // namespace vibe::service

#endif
