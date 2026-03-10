#ifndef VIBE_NET_REQUEST_PARSING_H
#define VIBE_NET_REQUEST_PARSING_H

#include <cstddef>
#include <optional>
#include <string>
#include <variant>

#include "vibe/auth/pairing.h"
#include "vibe/service/session_manager.h"
#include "vibe/session/launch_spec.h"

namespace vibe::net {

struct WebSocketInputCommand {
  std::string data;
};

struct WebSocketResizeCommand {
  vibe::session::TerminalSize terminal_size;
};

struct WebSocketStopCommand {};

struct WebSocketRequestControlCommand {
  vibe::session::ControllerKind controller_kind{vibe::session::ControllerKind::Remote};
};

struct WebSocketReleaseControlCommand {};

struct PairingRequestPayload {
  std::string device_name;
  vibe::auth::DeviceType device_type{vibe::auth::DeviceType::Unknown};
};

struct PairingApprovalPayload {
  std::string pairing_id;
  std::string code;
};

struct HostConfigPayload {
  std::string display_name;
};

using WebSocketCommand =
    std::variant<WebSocketInputCommand, WebSocketResizeCommand, WebSocketStopCommand,
                 WebSocketRequestControlCommand, WebSocketReleaseControlCommand>;

[[nodiscard]] auto ParseCreateSessionRequest(const std::string& body)
    -> std::optional<vibe::service::CreateSessionRequest>;
[[nodiscard]] auto ParseInputRequest(const std::string& body) -> std::optional<std::string>;
[[nodiscard]] auto ParsePairingRequest(const std::string& body) -> std::optional<PairingRequestPayload>;
[[nodiscard]] auto ParsePairingApprovalRequest(const std::string& body)
    -> std::optional<PairingApprovalPayload>;
[[nodiscard]] auto ParseHostConfigRequest(const std::string& body) -> std::optional<HostConfigPayload>;
[[nodiscard]] auto ParseWebSocketCommand(const std::string& body) -> std::optional<WebSocketCommand>;
[[nodiscard]] auto ParseTailBytes(const std::string& target) -> std::size_t;

}  // namespace vibe::net

#endif
