#include "vibe/net/http_server.h"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

#include "vibe/net/http_shared.h"

namespace vibe::net {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

HttpServer::HttpServer(std::string bind_address, const std::uint16_t port)
    : bind_address_(std::move(bind_address)),
      port_(port) {}

void HttpServer::Run() {
  asio::io_context io_context{1};
  const auto address = asio::ip::make_address(bind_address_);
  tcp::acceptor acceptor(io_context, tcp::endpoint(address, port_));

  std::cout << "HTTP server listening on " << bind_address_ << ":" << port_ << '\n';

  for (;;) {
    tcp::socket socket(io_context);
    acceptor.accept(socket);

    boost::beast::flat_buffer buffer;
    HttpRequest request;
    http::read(socket, buffer, request);

    HttpResponse response = HandleRequest(request);
    http::write(socket, response);

    boost::system::error_code error_code;
    socket.shutdown(tcp::socket::shutdown_send, error_code);
  }
}

}  // namespace vibe::net
