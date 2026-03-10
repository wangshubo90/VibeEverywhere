#ifndef VIBE_CLI_DAEMON_CLIENT_H
#define VIBE_CLI_DAEMON_CLIENT_H

#include <cstdint>
#include <optional>
#include <string>

#include "vibe/session/launch_spec.h"
#include "vibe/session/session_types.h"

namespace vibe::cli {

struct DaemonEndpoint {
  std::string host{"127.0.0.1"};
  std::uint16_t port{18085};
};

[[nodiscard]] auto BuildCreateSessionRequestBody(vibe::session::ProviderType provider,
                                                 const std::string& workspace_root,
                                                 const std::string& title) -> std::string;
[[nodiscard]] auto ParseCreatedSessionId(const std::string& body) -> std::optional<std::string>;
[[nodiscard]] auto BuildControlRequestCommand(vibe::session::ControllerKind controller_kind)
    -> std::string;
[[nodiscard]] auto BuildReleaseControlCommand() -> std::string;
[[nodiscard]] auto BuildInputCommand(const std::string& data) -> std::string;
[[nodiscard]] auto BuildResizeCommand(vibe::session::TerminalSize terminal_size) -> std::string;

[[nodiscard]] auto CreateSession(const DaemonEndpoint& endpoint,
                                 vibe::session::ProviderType provider,
                                 const std::string& workspace_root,
                                 const std::string& title) -> std::optional<std::string>;
[[nodiscard]] auto AttachSession(const DaemonEndpoint& endpoint, const std::string& session_id,
                                 vibe::session::ControllerKind controller_kind) -> int;

}  // namespace vibe::cli

#endif
