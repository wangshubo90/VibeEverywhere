#ifndef VIBE_CLI_DAEMON_CLIENT_H
#define VIBE_CLI_DAEMON_CLIENT_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "vibe/session/launch_spec.h"
#include "vibe/session/session_types.h"

namespace vibe::cli {

struct DaemonEndpoint {
  std::string host{"127.0.0.1"};
  std::uint16_t port{18085};
};

struct ListedSession {
  std::string session_id;
  std::string title;
  std::string activity_state;
  std::string status;
};

[[nodiscard]] auto BuildCreateSessionRequestBody(vibe::session::ProviderType provider,
                                                 const std::string& workspace_root,
                                                 const std::string& title) -> std::string;
[[nodiscard]] auto ParseCreatedSessionId(const std::string& body) -> std::optional<std::string>;
[[nodiscard]] auto ParseSessionList(const std::string& body) -> std::vector<ListedSession>;
[[nodiscard]] auto BuildControlRequestCommand(vibe::session::ControllerKind controller_kind)
    -> std::string;
[[nodiscard]] auto BuildReleaseControlCommand() -> std::string;
[[nodiscard]] auto BuildInputCommand(const std::string& data) -> std::string;
[[nodiscard]] auto BuildResizeCommand(vibe::session::TerminalSize terminal_size) -> std::string;

[[nodiscard]] auto CreateSession(const DaemonEndpoint& endpoint,
                                 vibe::session::ProviderType provider,
                                 const std::string& workspace_root,
                                 const std::string& title) -> std::optional<std::string>;
[[nodiscard]] auto ListSessions(const DaemonEndpoint& endpoint) -> std::optional<std::vector<ListedSession>>;
[[nodiscard]] auto GetSessionSnapshot(const DaemonEndpoint& endpoint, const std::string& session_id)
    -> std::optional<std::string>;
[[nodiscard]] auto StopSession(const DaemonEndpoint& endpoint, const std::string& session_id) -> std::optional<std::string>;
[[nodiscard]] auto ClearInactiveSessions(const DaemonEndpoint& endpoint) -> std::optional<std::string>;
[[nodiscard]] auto GetHostInfo(const DaemonEndpoint& endpoint) -> std::optional<std::string>;
[[nodiscard]] auto AttachSession(const DaemonEndpoint& endpoint, const std::string& session_id,
                                 vibe::session::ControllerKind controller_kind) -> int;
[[nodiscard]] auto ObserveSession(const DaemonEndpoint& endpoint, const std::string& session_id) -> int;

}  // namespace vibe::cli

#endif
