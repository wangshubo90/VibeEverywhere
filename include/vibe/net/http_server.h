#ifndef VIBE_NET_HTTP_SERVER_H
#define VIBE_NET_HTTP_SERVER_H

#include <cstdint>
#include <string>

namespace vibe::net {

class HttpServer {
 public:
  HttpServer(std::string bind_address, std::uint16_t port);

  void Run();

 private:
  std::string bind_address_;
  std::uint16_t port_;
};

}  // namespace vibe::net

#endif
