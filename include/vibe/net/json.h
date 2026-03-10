#ifndef VIBE_NET_JSON_H
#define VIBE_NET_JSON_H

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "vibe/auth/pairing.h"
#include "vibe/service/session_manager.h"
#include "vibe/session/session_snapshot.h"
#include "vibe/session/session_types.h"
#include "vibe/store/host_config_store.h"

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
  std::string session_id;
  std::string code;
  std::string message;
};

[[nodiscard]] auto JsonEscape(std::string_view input) -> std::string;
[[nodiscard]] auto Base64Encode(std::string_view input) -> std::string;
[[nodiscard]] auto ToJson(const vibe::service::SessionSummary& summary) -> std::string;
[[nodiscard]] auto ToJson(const std::vector<vibe::service::SessionSummary>& summaries) -> std::string;
[[nodiscard]] auto ToJson(const vibe::session::SessionSnapshot& snapshot) -> std::string;
[[nodiscard]] auto ToJson(const vibe::session::OutputSlice& slice) -> std::string;
[[nodiscard]] auto ToJson(const vibe::auth::PairingRequest& request) -> std::string;
[[nodiscard]] auto ToJson(const std::vector<vibe::auth::PairingRequest>& requests) -> std::string;
[[nodiscard]] auto ToJson(const vibe::auth::PairingRecord& record) -> std::string;
[[nodiscard]] auto ToJson(const TerminalOutputEvent& event) -> std::string;
[[nodiscard]] auto ToJson(const SessionUpdatedEvent& event) -> std::string;
[[nodiscard]] auto ToJson(const SessionExitedEvent& event) -> std::string;
[[nodiscard]] auto ToJson(const ErrorEvent& event) -> std::string;
[[nodiscard]] auto ToJsonHostInfo() -> std::string;
[[nodiscard]] auto ToJsonHostInfo(const std::optional<vibe::store::HostIdentity>& host_identity,
                                  bool tls_enabled) -> std::string;

}  // namespace vibe::net

#endif
