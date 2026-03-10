#ifndef VIBE_SERVICE_SESSION_MANAGER_H
#define VIBE_SERVICE_SESSION_MANAGER_H

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "vibe/service/git_inspector.h"
#include "vibe/session/launch_spec.h"
#include "vibe/session/posix_pty_process.h"
#include "vibe/session/session_runtime.h"
#include "vibe/session/session_snapshot.h"
#include "vibe/session/session_types.h"

namespace vibe::service {

struct CreateSessionRequest {
  vibe::session::ProviderType provider;
  std::string workspace_root;
  std::string title;
};

struct SessionSummary {
  vibe::session::SessionId id;
  vibe::session::ProviderType provider;
  std::string workspace_root;
  std::string title;
  vibe::session::SessionStatus status;
};

class SessionManager {
 public:
  SessionManager() = default;

  [[nodiscard]] auto CreateSession(const CreateSessionRequest& request)
      -> std::optional<SessionSummary>;
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
  void PollAll(int read_timeout_ms);

 private:
  struct SessionEntry {
    vibe::session::SessionId id;
    std::unique_ptr<vibe::session::PosixPtyProcess> process;
    std::unique_ptr<vibe::session::SessionRuntime> runtime;
    std::unique_ptr<vibe::service::GitInspector> git_inspector;
  };

  [[nodiscard]] auto MakeSessionId() const -> std::optional<vibe::session::SessionId>;
  [[nodiscard]] auto FindEntry(const std::string& session_id) -> SessionEntry*;
  [[nodiscard]] auto FindEntry(const std::string& session_id) const -> const SessionEntry*;

  std::vector<SessionEntry> sessions_;
  int poll_count_{0};
};

}  // namespace vibe::service

#endif
