#include "vibe/net/websocket_shared.h"

namespace vibe::net {

namespace {

auto StripQueryString(const std::string& target) -> std::string {
  const auto query_start = target.find('?');
  if (query_start == std::string::npos) {
    return target;
  }

  return target.substr(0, query_start);
}

auto ExtractQueryValue(const std::string& target, const std::string_view key) -> std::string {
  const auto query_start = target.find('?');
  if (query_start == std::string::npos || query_start + 1 >= target.size()) {
    return "";
  }

  const std::string_view query(target.c_str() + query_start + 1, target.size() - query_start - 1);
  std::size_t offset = 0;
  while (offset < query.size()) {
    const auto next = query.find('&', offset);
    const auto item = query.substr(offset, next == std::string_view::npos ? query.size() - offset : next - offset);
    const auto equals = item.find('=');
    const auto current_key = item.substr(0, equals);
    if (current_key == key) {
      if (equals == std::string_view::npos) {
        return "";
      }
      return std::string(item.substr(equals + 1));
    }
    if (next == std::string_view::npos) {
      break;
    }
    offset = next + 1;
  }

  return "";
}

}  // namespace

auto IsSessionWebSocketTarget(const std::string& target) -> bool {
  const std::string path = StripQueryString(target);
  return path.rfind("/ws/sessions/", 0) == 0 && path.size() > std::string("/ws/sessions/").size();
}

auto IsOverviewWebSocketTarget(const std::string& target) -> bool {
  return StripQueryString(target) == "/ws/overview";
}

auto ExtractSessionIdFromWebSocketTarget(const std::string& target) -> std::string {
  constexpr auto prefix = "/ws/sessions/";
  if (!IsSessionWebSocketTarget(target)) {
    return "";
  }

  const std::string path = StripQueryString(target);
  return path.substr(std::string(prefix).size());
}

auto ExtractAccessTokenFromWebSocketTarget(const std::string& target) -> std::string {
  if (!IsSessionWebSocketTarget(target) && !IsOverviewWebSocketTarget(target)) {
    return "";
  }

  return ExtractQueryValue(target, "access_token");
}

}  // namespace vibe::net
