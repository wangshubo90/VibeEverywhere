#include "vibe/session/session_types.h"

#include <algorithm>
#include <cctype>

namespace vibe::session {

namespace {

auto IsValidSessionIdChar(const char ch) -> bool {
  return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_' || ch == '-';
}

}  // namespace

auto SessionId::TryCreate(std::string value) -> std::optional<SessionId> {
  if (value.empty()) {
    return std::nullopt;
  }

  const auto is_valid = std::ranges::all_of(value, IsValidSessionIdChar);
  if (!is_valid) {
    return std::nullopt;
  }

  return SessionId(std::move(value));
}

SessionId::SessionId(std::string value) : value_(std::move(value)) {}

auto SessionId::value() const -> const std::string& { return value_; }

auto ToString(ProviderType provider) -> std::string_view {
  switch (provider) {
    case ProviderType::Codex:
      return "codex";
    case ProviderType::Claude:
      return "claude";
  }

  return "unknown";
}

auto ToString(SessionStatus status) -> std::string_view {
  switch (status) {
    case SessionStatus::Created:
      return "Created";
    case SessionStatus::Starting:
      return "Starting";
    case SessionStatus::Running:
      return "Running";
    case SessionStatus::AwaitingInput:
      return "AwaitingInput";
    case SessionStatus::Exited:
      return "Exited";
    case SessionStatus::Error:
      return "Error";
  }

  return "Unknown";
}

auto ToString(ControllerKind controller_kind) -> std::string_view {
  switch (controller_kind) {
    case ControllerKind::None:
      return "none";
    case ControllerKind::Host:
      return "host";
    case ControllerKind::Remote:
      return "remote";
  }

  return "unknown";
}

}  // namespace vibe::session
