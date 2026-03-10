#ifndef VIBE_NET_REQUEST_PARSING_H
#define VIBE_NET_REQUEST_PARSING_H

#include <cstddef>
#include <optional>
#include <string>
#include <variant>

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

using WebSocketCommand =
    std::variant<WebSocketInputCommand, WebSocketResizeCommand, WebSocketStopCommand>;

[[nodiscard]] auto ParseCreateSessionRequest(const std::string& body)
    -> std::optional<vibe::service::CreateSessionRequest>;
[[nodiscard]] auto ParseInputRequest(const std::string& body) -> std::optional<std::string>;
[[nodiscard]] auto ParseWebSocketCommand(const std::string& body) -> std::optional<WebSocketCommand>;
[[nodiscard]] auto ParseTailBytes(const std::string& target) -> std::size_t;

}  // namespace vibe::net

#endif
