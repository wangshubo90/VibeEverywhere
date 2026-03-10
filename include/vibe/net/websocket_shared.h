#ifndef VIBE_NET_WEBSOCKET_SHARED_H
#define VIBE_NET_WEBSOCKET_SHARED_H

#include <string>

namespace vibe::net {

[[nodiscard]] auto IsSessionWebSocketTarget(const std::string& target) -> bool;
[[nodiscard]] auto ExtractSessionIdFromWebSocketTarget(const std::string& target) -> std::string;
[[nodiscard]] auto ExtractAccessTokenFromWebSocketTarget(const std::string& target) -> std::string;

}  // namespace vibe::net

#endif
