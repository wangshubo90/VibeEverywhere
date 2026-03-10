#include <gtest/gtest.h>

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/websocket.hpp>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include "vibe/net/http_server.h"

namespace vibe::net {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;
namespace http = beast::http;

class HttpServerFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    server_thread_ = std::thread([this]() {
      HttpServer server("127.0.0.1", 18088);
      server_ = &server;
      started_.store(true);
      const bool ran = server.Run();
      static_cast<void>(ran);
      server_ = nullptr;
    });

    while (!started_.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  void TearDown() override {
    if (server_ != nullptr) {
      server_->Stop();
    }
    if (server_thread_.joinable()) {
      server_thread_.join();
    }
  }

  static auto CreateSession() -> std::string {
    asio::io_context io_context;
    tcp::resolver resolver(io_context);
    tcp::socket socket(io_context);
    const auto results = resolver.resolve("127.0.0.1", "18088");
    asio::connect(socket, results);

    http::request<http::string_body> request{http::verb::post, "/sessions", 11};
    request.set(http::field::host, "127.0.0.1");
    request.set(http::field::content_type, "application/json");
    request.body() = "{\"provider\":\"codex\",\"workspaceRoot\":\".\",\"title\":\"ws-session\"}";
    request.prepare_payload();

    http::write(socket, request);
    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(socket, buffer, response);
    boost::system::error_code error_code;
    socket.shutdown(tcp::socket::shutdown_both, error_code);
    return response.body();
  }

  std::atomic<bool> started_{false};
  HttpServer* server_{nullptr};
  std::thread server_thread_;
};

TEST_F(HttpServerFixture, WebSocketSessionEndpointStreamsTerminalOutput) {
  const std::string create_response = CreateSession();
  ASSERT_NE(create_response.find("\"sessionId\":\"s_1\""), std::string::npos);

  asio::io_context io_context;
  tcp::resolver resolver(io_context);
  websocket::stream<tcp::socket> websocket(io_context);
  const auto results = resolver.resolve("127.0.0.1", "18088");
  auto endpoint = asio::connect(websocket.next_layer(), results);
  static_cast<void>(endpoint);

  websocket.handshake("127.0.0.1:18088", "/ws/sessions/s_1");

  beast::flat_buffer buffer;
  websocket.read(buffer);
  const std::string payload = beast::buffers_to_string(buffer.data());

  EXPECT_NE(payload.find("\"seqStart\""), std::string::npos);
  EXPECT_NE(payload.find("\"data\""), std::string::npos);
}

}  // namespace
}  // namespace vibe::net
