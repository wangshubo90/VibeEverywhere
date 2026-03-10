#include "vibe/net/websocket_shared.h"

namespace vibe::net {

auto IsSessionWebSocketTarget(const std::string& target) -> bool {
  return target.rfind("/ws/sessions/", 0) == 0 && target.size() > std::string("/ws/sessions/").size();
}

auto ExtractSessionIdFromWebSocketTarget(const std::string& target) -> std::string {
  constexpr auto prefix = "/ws/sessions/";
  if (!IsSessionWebSocketTarget(target)) {
    return "";
  }

  return target.substr(std::string(prefix).size());
}

}  // namespace vibe::net
