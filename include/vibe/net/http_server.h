#ifndef VIBE_NET_HTTP_SERVER_H
#define VIBE_NET_HTTP_SERVER_H

#include <cstdint>
#include <memory>
#include <string>

#include <boost/asio/io_context.hpp>

#include "vibe/service/session_manager.h"

namespace vibe::net {

class HttpServer {
 public:
  HttpServer(std::string bind_address, std::uint16_t port);
  ~HttpServer();

  [[nodiscard]] auto Run() -> bool;
  void Stop();

 private:
  std::string bind_address_;
  std::uint16_t port_;
  vibe::service::SessionManager session_manager_;
  std::unique_ptr<boost::asio::io_context> io_context_;
};

}  // namespace vibe::net

#endif
