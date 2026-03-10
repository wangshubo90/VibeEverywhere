#ifndef VIBE_STORE_HOST_CONFIG_STORE_H
#define VIBE_STORE_HOST_CONFIG_STORE_H

#include <optional>
#include <string>

namespace vibe::store {

struct HostIdentity {
  std::string host_id;
  std::string display_name;
  std::string certificate_pem_path;
  std::string private_key_pem_path;

  [[nodiscard]] auto operator==(const HostIdentity& other) const -> bool = default;
};

class HostConfigStore {
 public:
  virtual ~HostConfigStore() = default;

  [[nodiscard]] virtual auto LoadHostIdentity() const -> std::optional<HostIdentity> = 0;
  [[nodiscard]] virtual auto SaveHostIdentity(const HostIdentity& identity) -> bool = 0;
};

}  // namespace vibe::store

#endif
