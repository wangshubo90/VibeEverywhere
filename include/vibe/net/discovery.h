#ifndef VIBE_NET_DISCOVERY_H
#define VIBE_NET_DISCOVERY_H

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "vibe/store/host_config_store.h"

namespace vibe::net {

inline constexpr std::uint16_t kDefaultDiscoveryPort = 18087;
inline constexpr std::chrono::milliseconds kDefaultDiscoveryBroadcastInterval{5000};
inline constexpr std::string_view kDiscoveryProtocolVersion = "1";

struct DiscoveryInfo {
  std::string host_id;
  std::string display_name;
  std::string remote_host;
  std::uint16_t remote_port{0};
  std::string protocol_version{std::string(kDiscoveryProtocolVersion)};
  bool tls{false};

  [[nodiscard]] auto operator==(const DiscoveryInfo& other) const -> bool = default;
};

[[nodiscard]] auto ResolveDiscoveryInfo(const std::optional<vibe::store::HostIdentity>& host_identity,
                                        std::string_view remote_host,
                                        std::uint16_t remote_port,
                                        bool tls_enabled) -> DiscoveryInfo;

}  // namespace vibe::net

#endif
