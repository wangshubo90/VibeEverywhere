#ifndef VIBE_NET_HTTP_SERVER_H
#define VIBE_NET_HTTP_SERVER_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include <boost/asio/io_context.hpp>

#include "vibe/auth/authorizer.h"
#include "vibe/auth/pairing.h"
#include "vibe/service/session_manager.h"
#include "vibe/store/host_config_store.h"
#include "vibe/store/pairing_store.h"

namespace vibe::net {

class HttpServer {
 public:
  HttpServer(std::string bind_address, std::uint16_t port);
  HttpServer(std::string bind_address, std::uint16_t port, std::filesystem::path storage_root);
  ~HttpServer();

  [[nodiscard]] auto Run() -> bool;
  void Stop();

 private:
  std::string bind_address_;
  std::uint16_t port_;
  std::filesystem::path storage_root_;
  vibe::service::SessionManager session_manager_;
  std::shared_ptr<vibe::auth::Authorizer> authorizer_;
  std::shared_ptr<vibe::auth::PairingService> pairing_service_;
  std::shared_ptr<vibe::store::PairingStore> pairing_store_;
  std::shared_ptr<vibe::store::HostConfigStore> host_config_store_;
  std::unique_ptr<boost::asio::io_context> io_context_;
};

}  // namespace vibe::net

#endif
