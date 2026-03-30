#ifndef VIBE_STORE_HOST_CONFIG_STORE_H
#define VIBE_STORE_HOST_CONFIG_STORE_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vibe::store {

inline constexpr std::string_view kDefaultAdminHost = "127.0.0.1";
inline constexpr std::uint16_t kDefaultAdminPort = 18085;
inline constexpr std::string_view kDefaultRemoteHost = "0.0.0.0";
inline constexpr std::uint16_t kDefaultRemotePort = 18086;

struct ProviderCommandOverride {
  std::string executable;
  std::vector<std::string> args;

  [[nodiscard]] auto operator==(const ProviderCommandOverride& other) const -> bool = default;
};

struct HostIdentity {
  std::string host_id;
  std::string display_name;
  std::string certificate_pem_path;
  std::string private_key_pem_path;
  std::string admin_host{std::string(kDefaultAdminHost)};
  std::uint16_t admin_port{kDefaultAdminPort};
  std::string remote_host{std::string(kDefaultRemoteHost)};
  std::uint16_t remote_port{kDefaultRemotePort};
  ProviderCommandOverride codex_command;
  ProviderCommandOverride claude_command;

  [[nodiscard]] auto operator==(const HostIdentity& other) const -> bool = default;
};

[[nodiscard]] inline auto MakeDefaultHostIdentity() -> HostIdentity {
  return HostIdentity{
      .host_id = "local-dev-host",
      .display_name = "Sentrits Dev Host",
      .certificate_pem_path = "",
      .private_key_pem_path = "",
      .admin_host = std::string(kDefaultAdminHost),
      .admin_port = kDefaultAdminPort,
      .remote_host = std::string(kDefaultRemoteHost),
      .remote_port = kDefaultRemotePort,
      .codex_command = {},
      .claude_command = {},
  };
}

class HostConfigStore {
 public:
  virtual ~HostConfigStore() = default;

  [[nodiscard]] virtual auto LoadHostIdentity() const -> std::optional<HostIdentity> = 0;
  [[nodiscard]] virtual auto SaveHostIdentity(const HostIdentity& identity) -> bool = 0;
};

}  // namespace vibe::store

#endif
