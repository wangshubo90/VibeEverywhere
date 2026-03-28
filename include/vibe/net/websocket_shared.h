#ifndef VIBE_NET_WEBSOCKET_SHARED_H
#define VIBE_NET_WEBSOCKET_SHARED_H

#include <cstdint>
#include <string>

namespace vibe::net {

class StreamSequenceWindow {
 public:
  [[nodiscard]] auto delivered_next() const -> std::uint64_t;
  [[nodiscard]] auto next_request_sequence() const -> std::uint64_t;
  void ReserveThrough(std::uint64_t next_sequence);
  void MarkDelivered(std::uint64_t next_sequence);

 private:
  std::uint64_t delivered_next_{1};
  std::uint64_t reserved_next_{1};
};

[[nodiscard]] auto IsSessionWebSocketTarget(const std::string& target) -> bool;
[[nodiscard]] auto IsOverviewWebSocketTarget(const std::string& target) -> bool;
[[nodiscard]] auto ExtractSessionIdFromWebSocketTarget(const std::string& target) -> std::string;
[[nodiscard]] auto ExtractAccessTokenFromWebSocketTarget(const std::string& target) -> std::string;

}  // namespace vibe::net

#endif
