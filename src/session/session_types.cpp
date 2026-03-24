#include "vibe/session/session_types.h"

#include <algorithm>
#include <cctype>

namespace vibe::session {

namespace {

auto IsValidSessionIdChar(const char ch) -> bool {
  return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_' || ch == '-';
}

auto IsAsciiWhitespace(const char ch) -> bool {
  return std::isspace(static_cast<unsigned char>(ch)) != 0;
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

auto NormalizeGroupTag(const std::string_view tag) -> std::string {
  std::size_t start = 0;
  while (start < tag.size() && IsAsciiWhitespace(tag[start])) {
    start += 1;
  }

  std::size_t end = tag.size();
  while (end > start && IsAsciiWhitespace(tag[end - 1])) {
    end -= 1;
  }

  std::string normalized;
  normalized.reserve(end - start);
  for (std::size_t index = start; index < end; ++index) {
    normalized.push_back(static_cast<char>(
        std::tolower(static_cast<unsigned char>(tag[index]))));
  }

  return normalized;
}

auto NormalizeGroupTags(const std::vector<std::string>& tags) -> std::vector<std::string> {
  std::vector<std::string> normalized_tags;
  normalized_tags.reserve(tags.size());

  for (const auto& tag : tags) {
    const std::string normalized = NormalizeGroupTag(tag);
    if (normalized.empty()) {
      continue;
    }
    if (std::ranges::find(normalized_tags, normalized) != normalized_tags.end()) {
      continue;
    }
    normalized_tags.push_back(normalized);
  }

  return normalized_tags;
}

}  // namespace vibe::session
