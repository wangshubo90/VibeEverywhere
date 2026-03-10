#ifndef VIBE_NET_HOST_ADMIN_H
#define VIBE_NET_HOST_ADMIN_H

#include <string>
#include <vector>

#include "vibe/session/session_types.h"

namespace vibe::net {

struct AttachedClientInfo {
  std::string client_id;
  std::string session_id;
  std::string client_address;
  vibe::session::ControllerKind claimed_kind{vibe::session::ControllerKind::None};
  bool is_local{false};
  bool has_control{false};

  [[nodiscard]] auto operator==(const AttachedClientInfo& other) const -> bool = default;
};

class HostAdmin {
 public:
  virtual ~HostAdmin() = default;

  [[nodiscard]] virtual auto ListAttachedClients() const -> std::vector<AttachedClientInfo> = 0;
  [[nodiscard]] virtual auto DisconnectClient(const std::string& client_id) -> bool = 0;
};

}  // namespace vibe::net

#endif
