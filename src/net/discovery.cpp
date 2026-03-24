#include "vibe/net/discovery.h"

namespace vibe::net {

auto ResolveDiscoveryInfo(const std::optional<vibe::store::HostIdentity>& host_identity,
                          const std::string_view remote_host,
                          const std::uint16_t remote_port,
                          const bool tls_enabled) -> DiscoveryInfo {
  const vibe::store::HostIdentity resolved_identity =
      host_identity.value_or(vibe::store::MakeDefaultHostIdentity());

  return DiscoveryInfo{
      .host_id = resolved_identity.host_id,
      .display_name = resolved_identity.display_name,
      .remote_host = remote_host.empty() ? resolved_identity.remote_host : std::string(remote_host),
      .remote_port = remote_port == 0 ? resolved_identity.remote_port : remote_port,
      .protocol_version = std::string(kDiscoveryProtocolVersion),
      .tls = tls_enabled,
  };
}

}  // namespace vibe::net
