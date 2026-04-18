#include "vibe/net/local_auth.h"

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#include "vibe/auth/default_authorizer.h"
#include "vibe/auth/default_pairing_service.h"
#include "vibe/store/file_stores.h"

namespace vibe::net {

auto DefaultStorageRoot() -> std::filesystem::path {
  if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
    return std::filesystem::path(home) / ".sentrits";
  }

  return std::filesystem::current_path() / ".sentrits";
}

auto DefaultControllerSocketPath(const std::filesystem::path& storage_root)
    -> std::filesystem::path {
  return storage_root / "controller.sock";
}

auto DefaultLogDirectory(const std::filesystem::path& storage_root)
    -> std::filesystem::path {
  return storage_root / "logs";
}

auto DefaultServeLogPath(const std::filesystem::path& storage_root)
    -> std::filesystem::path {
  return DefaultLogDirectory(storage_root) / "sentrits.log";
}

auto CreateLocalAuthServices(const std::filesystem::path& storage_root) -> LocalAuthServices {
  auto pairing_store = std::make_shared<vibe::store::FilePairingStore>(storage_root);
  auto host_config_store = std::make_shared<vibe::store::FileHostConfigStore>(storage_root);
  static_cast<void>(vibe::store::EnsureHostIdentity(*host_config_store));

  return LocalAuthServices{
      .authorizer = std::make_shared<vibe::auth::DefaultAuthorizer>(*pairing_store),
      .pairing_service = std::make_shared<vibe::auth::DefaultPairingService>(*pairing_store),
      .pairing_store = std::move(pairing_store),
      .host_config_store = std::move(host_config_store),
  };
}

}  // namespace vibe::net
