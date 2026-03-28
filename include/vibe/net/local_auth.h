#ifndef VIBE_NET_LOCAL_AUTH_H
#define VIBE_NET_LOCAL_AUTH_H

#include <filesystem>
#include <memory>

#include "vibe/auth/authorizer.h"
#include "vibe/auth/pairing.h"
#include "vibe/store/host_config_store.h"
#include "vibe/store/pairing_store.h"

namespace vibe::net {

struct LocalAuthServices {
  std::shared_ptr<vibe::auth::Authorizer> authorizer;
  std::shared_ptr<vibe::auth::PairingService> pairing_service;
  std::shared_ptr<vibe::store::PairingStore> pairing_store;
  std::shared_ptr<vibe::store::HostConfigStore> host_config_store;
};

[[nodiscard]] auto DefaultStorageRoot() -> std::filesystem::path;
[[nodiscard]] auto DefaultControllerSocketPath(const std::filesystem::path& storage_root)
    -> std::filesystem::path;
[[nodiscard]] auto CreateLocalAuthServices(const std::filesystem::path& storage_root) -> LocalAuthServices;

}  // namespace vibe::net

#endif
