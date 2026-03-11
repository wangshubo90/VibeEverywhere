#ifndef VIBE_STORE_SESSION_STORE_H
#define VIBE_STORE_SESSION_STORE_H

#include <optional>
#include <string>
#include <vector>

#include "vibe/session/session_types.h"

namespace vibe::store {

struct PersistedSessionRecord {
  std::string session_id;
  vibe::session::ProviderType provider{vibe::session::ProviderType::Codex};
  std::string workspace_root;
  std::string title;
  vibe::session::SessionStatus status{vibe::session::SessionStatus::Created};
  std::optional<std::string> conversation_id;
  std::uint64_t current_sequence{0};
  std::string recent_terminal_tail;

  [[nodiscard]] auto operator==(const PersistedSessionRecord& other) const -> bool = default;
};

class SessionStore {
 public:
  virtual ~SessionStore() = default;

  [[nodiscard]] virtual auto LoadSessions() const -> std::vector<PersistedSessionRecord> = 0;
  [[nodiscard]] virtual auto UpsertSessionRecord(const PersistedSessionRecord& record) -> bool = 0;
  [[nodiscard]] virtual auto RemoveSessionRecord(const std::string& session_id) -> bool = 0;
};

}  // namespace vibe::store

#endif
