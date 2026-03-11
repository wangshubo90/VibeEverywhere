#ifndef VIBE_SESSION_SESSION_TYPES_H
#define VIBE_SESSION_SESSION_TYPES_H

#include <optional>
#include <string>
#include <string_view>

namespace vibe::session {

enum class ProviderType {
  Codex,
  Claude,
};

enum class SessionStatus {
  Created,
  Starting,
  Running,
  AwaitingInput,
  Exited,
  Error,
};

enum class ControllerKind {
  None,
  Host,
  Remote,
};

class SessionId {
 public:
  static auto TryCreate(std::string value) -> std::optional<SessionId>;

  explicit SessionId(std::string value);

  [[nodiscard]] auto value() const -> const std::string&;
  [[nodiscard]] auto operator==(const SessionId& other) const -> bool = default;

 private:
  std::string value_;
};

struct SessionMetadata {
  SessionId id;
  ProviderType provider;
  std::string workspace_root;
  std::string title;
  SessionStatus status{SessionStatus::Created};
  std::optional<std::string> conversation_id;
};

[[nodiscard]] auto ToString(ProviderType provider) -> std::string_view;
[[nodiscard]] auto ToString(SessionStatus status) -> std::string_view;
[[nodiscard]] auto ToString(ControllerKind controller_kind) -> std::string_view;

}  // namespace vibe::session

#endif
