#ifndef VIBE_NET_HTTP_SERVER_H
#define VIBE_NET_HTTP_SERVER_H

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <mutex>
#include <string>

#include <boost/asio/io_context.hpp>

#include "vibe/auth/authorizer.h"
#include "vibe/auth/pairing.h"
#include "vibe/service/session_manager.h"
#include "vibe/store/file_stores.h"
#include "vibe/store/host_config_store.h"
#include "vibe/store/pairing_store.h"

namespace vibe::net {

struct RemoteTlsFiles {
  std::string certificate_pem_path;
  std::string private_key_pem_path;
};

class HttpServer {
 public:
  HttpServer(std::string admin_bind_address, std::uint16_t admin_port,
             std::string remote_bind_address, std::uint16_t remote_port,
             std::optional<RemoteTlsFiles> remote_tls_override = std::nullopt,
             bool enable_discovery = true);
  HttpServer(std::string admin_bind_address, std::uint16_t admin_port,
             std::string remote_bind_address, std::uint16_t remote_port,
             std::filesystem::path storage_root,
             std::optional<RemoteTlsFiles> remote_tls_override = std::nullopt,
             bool enable_discovery = true);
  ~HttpServer();

  [[nodiscard]] auto Run() -> bool;
  void Stop();

 private:
  std::string admin_bind_address_;
  std::uint16_t admin_port_;
  std::string remote_bind_address_;
  std::uint16_t remote_port_;
  std::filesystem::path storage_root_;
  std::optional<RemoteTlsFiles> remote_tls_override_;
  bool enable_discovery_{true};
  vibe::store::FileSessionStore session_store_;
  vibe::service::SessionManager session_manager_;
  std::shared_ptr<vibe::auth::Authorizer> authorizer_;
  std::shared_ptr<vibe::auth::PairingService> pairing_service_;
  std::shared_ptr<vibe::store::PairingStore> pairing_store_;
  std::shared_ptr<vibe::store::HostConfigStore> host_config_store_;
  std::unique_ptr<boost::asio::io_context> io_context_;
  std::function<void()> stop_callback_;
  mutable std::mutex state_mutex_;
  bool stopping_{false};
};

}  // namespace vibe::net

#endif
