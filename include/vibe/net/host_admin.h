#ifndef VIBE_NET_HOST_ADMIN_H
#define VIBE_NET_HOST_ADMIN_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "vibe/session/session_types.h"

namespace vibe::net {

struct AttachedClientInfo {
  std::string client_id;
  std::string session_id;
  std::string session_title;
  std::string client_address;
  vibe::session::SessionStatus session_status{vibe::session::SessionStatus::Created};
  bool session_is_recovered{false};
  vibe::session::ControllerKind claimed_kind{vibe::session::ControllerKind::None};
  bool is_local{false};
  bool has_control{false};
  std::optional<std::int64_t> connected_at_unix_ms;

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
