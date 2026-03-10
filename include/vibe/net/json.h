#ifndef VIBE_NET_JSON_H
#define VIBE_NET_JSON_H

#include <string>
#include <string_view>
#include <vector>

#include "vibe/service/session_manager.h"
#include "vibe/session/session_snapshot.h"
#include "vibe/session/session_types.h"

namespace vibe::net {

struct TerminalOutputEvent {
  std::string session_id;
  vibe::session::OutputSlice slice;
};

struct SessionUpdatedEvent {
  vibe::service::SessionSummary summary;
};

struct SessionExitedEvent {
  std::string session_id;
  vibe::session::SessionStatus status;
};

struct ErrorEvent {
  std::string code;
  std::string message;
};

[[nodiscard]] auto JsonEscape(std::string_view input) -> std::string;
[[nodiscard]] auto ToJson(const vibe::service::SessionSummary& summary) -> std::string;
[[nodiscard]] auto ToJson(const std::vector<vibe::service::SessionSummary>& summaries) -> std::string;
[[nodiscard]] auto ToJson(const vibe::session::SessionSnapshot& snapshot) -> std::string;
[[nodiscard]] auto ToJson(const vibe::session::OutputSlice& slice) -> std::string;
[[nodiscard]] auto ToJson(const TerminalOutputEvent& event) -> std::string;
[[nodiscard]] auto ToJson(const SessionUpdatedEvent& event) -> std::string;
[[nodiscard]] auto ToJson(const SessionExitedEvent& event) -> std::string;
[[nodiscard]] auto ToJson(const ErrorEvent& event) -> std::string;
[[nodiscard]] auto ToJsonHostInfo() -> std::string;

}  // namespace vibe::net

#endif
