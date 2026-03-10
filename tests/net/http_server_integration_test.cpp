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

auto ExtractSessionId(const std::string& create_response) -> std::string {
  const std::string marker = "\"sessionId\":\"";
  const auto start = create_response.find(marker);
  if (start == std::string::npos) {
    return {};
  }

  const auto value_start = start + marker.size();
  const auto value_end = create_response.find('"', value_start);
  if (value_end == std::string::npos) {
    return {};
  }

  return create_response.substr(value_start, value_end - value_start);
}

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
  const std::string first_payload = beast::buffers_to_string(buffer.data());
  EXPECT_NE(first_payload.find("\"type\":\"session.updated\""), std::string::npos);
  EXPECT_NE(first_payload.find("\"sessionId\":\"s_1\""), std::string::npos);
  EXPECT_NE(first_payload.find("\"status\""), std::string::npos);
  EXPECT_NE(first_payload.find("\"controllerKind\":\"host\""), std::string::npos);

  buffer.consume(buffer.size());
  websocket.read(buffer);
  const std::string second_payload = beast::buffers_to_string(buffer.data());
  EXPECT_NE(second_payload.find("\"type\":\"terminal.output\""), std::string::npos);
  EXPECT_NE(second_payload.find("\"sessionId\":\"s_1\""), std::string::npos);
  EXPECT_NE(second_payload.find("\"seqStart\""), std::string::npos);
  EXPECT_NE(second_payload.find("\"dataEncoding\":\"base64\""), std::string::npos);
  EXPECT_NE(second_payload.find("\"dataBase64\""), std::string::npos);
}

TEST_F(HttpServerFixture, WebSocketSessionEndpointStreamsExitEventsAfterStop) {
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
  buffer.consume(buffer.size());
  websocket.read(buffer);
  buffer.consume(buffer.size());

  asio::io_context stop_io_context;
  tcp::resolver stop_resolver(stop_io_context);
  tcp::socket stop_socket(stop_io_context);
  const auto stop_results = stop_resolver.resolve("127.0.0.1", "18088");
  asio::connect(stop_socket, stop_results);

  http::request<http::string_body> stop_request{http::verb::post, "/sessions/s_1/stop", 11};
  stop_request.set(http::field::host, "127.0.0.1");
  stop_request.prepare_payload();
  http::write(stop_socket, stop_request);
  beast::flat_buffer stop_buffer;
  http::response<http::string_body> stop_response;
  http::read(stop_socket, stop_buffer, stop_response);
  EXPECT_EQ(stop_response.result(), http::status::ok);

  websocket.read(buffer);
  const std::string updated_payload = beast::buffers_to_string(buffer.data());
  EXPECT_NE(updated_payload.find("\"type\":\"session.updated\""), std::string::npos);
  EXPECT_NE(updated_payload.find("\"status\":\"Exited\""), std::string::npos);

  buffer.consume(buffer.size());
  websocket.read(buffer);
  const std::string exited_payload = beast::buffers_to_string(buffer.data());
  EXPECT_NE(exited_payload.find("\"type\":\"session.exited\""), std::string::npos);
  EXPECT_NE(exited_payload.find("\"sessionId\":\"s_1\""), std::string::npos);
  EXPECT_NE(exited_payload.find("\"status\":\"Exited\""), std::string::npos);
}

TEST_F(HttpServerFixture, WebSocketSessionEndpointRejectsInvalidCommands) {
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
  buffer.consume(buffer.size());
  websocket.read(buffer);
  buffer.consume(buffer.size());

  websocket.write(asio::buffer(std::string(R"({"type":"unknown"})")));

  websocket.read(buffer);
  const std::string payload = beast::buffers_to_string(buffer.data());
  EXPECT_NE(payload.find("\"type\":\"error\""), std::string::npos);
  EXPECT_NE(payload.find("\"code\":\"invalid_command\""), std::string::npos);
}

TEST_F(HttpServerFixture, WebSocketSessionEndpointAcceptsStopCommand) {
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
  buffer.consume(buffer.size());
  websocket.read(buffer);
  buffer.consume(buffer.size());

  websocket.write(asio::buffer(std::string(R"({"type":"session.control.request"})")));
  websocket.read(buffer);
  const std::string control_payload = beast::buffers_to_string(buffer.data());
  EXPECT_NE(control_payload.find("\"type\":\"session.updated\""), std::string::npos);
  EXPECT_NE(control_payload.find("\"controllerKind\":\"remote\""), std::string::npos);

  buffer.consume(buffer.size());
  websocket.write(asio::buffer(std::string(R"({"type":"session.stop"})")));

  websocket.read(buffer);
  const std::string updated_payload = beast::buffers_to_string(buffer.data());
  EXPECT_NE(updated_payload.find("\"type\":\"session.updated\""), std::string::npos);
  EXPECT_NE(updated_payload.find("\"status\":\"Exited\""), std::string::npos);

  buffer.consume(buffer.size());
  websocket.read(buffer);
  const std::string exited_payload = beast::buffers_to_string(buffer.data());
  EXPECT_NE(exited_payload.find("\"type\":\"session.exited\""), std::string::npos);
  EXPECT_NE(exited_payload.find("\"status\":\"Exited\""), std::string::npos);
}

TEST_F(HttpServerFixture, WebSocketSessionEndpointRejectsMutationWithoutControl) {
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
  buffer.consume(buffer.size());
  websocket.read(buffer);
  buffer.consume(buffer.size());

  websocket.write(asio::buffer(std::string(R"({"type":"session.stop"})")));
  websocket.read(buffer);
  const std::string payload = beast::buffers_to_string(buffer.data());
  EXPECT_NE(payload.find("\"type\":\"error\""), std::string::npos);
  EXPECT_NE(payload.find("\"code\":\"command_rejected\""), std::string::npos);
}

TEST_F(HttpServerFixture, WebSocketSessionEndpointReleasesControlBackToHost) {
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
  buffer.consume(buffer.size());
  websocket.read(buffer);
  buffer.consume(buffer.size());

  websocket.write(asio::buffer(std::string(R"({"type":"session.control.request"})")));
  websocket.read(buffer);
  const std::string request_payload = beast::buffers_to_string(buffer.data());
  EXPECT_NE(request_payload.find("\"controllerKind\":\"remote\""), std::string::npos);

  buffer.consume(buffer.size());
  websocket.write(asio::buffer(std::string(R"({"type":"session.control.release"})")));
  websocket.read(buffer);
  const std::string release_payload = beast::buffers_to_string(buffer.data());
  EXPECT_NE(release_payload.find("\"type\":\"session.updated\""), std::string::npos);
  EXPECT_NE(release_payload.find("\"controllerKind\":\"host\""), std::string::npos);
}

TEST_F(HttpServerFixture, HostDetachClearsStaleHostControllerClaim) {
  const std::string create_response = CreateSession();
  const std::string session_id = ExtractSessionId(create_response);
  ASSERT_FALSE(session_id.empty());

  asio::io_context first_io_context;
  tcp::resolver first_resolver(first_io_context);
  websocket::stream<tcp::socket> first_websocket(first_io_context);
  const auto first_results = first_resolver.resolve("127.0.0.1", "18088");
  auto first_endpoint = asio::connect(first_websocket.next_layer(), first_results);
  static_cast<void>(first_endpoint);

  first_websocket.handshake("127.0.0.1:18088", "/ws/sessions/" + session_id);

  beast::flat_buffer first_buffer;
  first_websocket.read(first_buffer);
  first_buffer.consume(first_buffer.size());
  first_websocket.read(first_buffer);
  first_buffer.consume(first_buffer.size());

  first_websocket.write(asio::buffer(std::string(R"({"type":"session.control.request","kind":"host"})")));
  first_websocket.read(first_buffer);
  const std::string claimed_payload = beast::buffers_to_string(first_buffer.data());
  EXPECT_NE(claimed_payload.find("\"controllerKind\":\"host\""), std::string::npos);
  EXPECT_NE(claimed_payload.find("\"controllerClientId\":"), std::string::npos);

  boost::system::error_code close_error;
  first_websocket.close(websocket::close_code::normal, close_error);
  EXPECT_FALSE(close_error);

  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  asio::io_context second_io_context;
  tcp::resolver second_resolver(second_io_context);
  websocket::stream<tcp::socket> second_websocket(second_io_context);
  const auto second_results = second_resolver.resolve("127.0.0.1", "18088");
  auto second_endpoint = asio::connect(second_websocket.next_layer(), second_results);
  static_cast<void>(second_endpoint);

  second_websocket.handshake("127.0.0.1:18088", "/ws/sessions/" + session_id);

  beast::flat_buffer second_buffer;
  second_websocket.read(second_buffer);
  const std::string updated_payload = beast::buffers_to_string(second_buffer.data());
  EXPECT_NE(updated_payload.find("\"type\":\"session.updated\""), std::string::npos);
  EXPECT_NE(updated_payload.find("\"controllerKind\":\"host\""), std::string::npos);
  EXPECT_EQ(updated_payload.find("\"controllerClientId\":"), std::string::npos);
}

TEST_F(HttpServerFixture, RemoteControlReturnsToHostAfterControllerDisconnects) {
  const std::string create_response = CreateSession();
  const std::string session_id = ExtractSessionId(create_response);
  ASSERT_FALSE(session_id.empty());

  asio::io_context first_io_context;
  tcp::resolver first_resolver(first_io_context);
  websocket::stream<tcp::socket> first_websocket(first_io_context);
  const auto first_results = first_resolver.resolve("127.0.0.1", "18088");
  auto first_endpoint = asio::connect(first_websocket.next_layer(), first_results);
  static_cast<void>(first_endpoint);

  first_websocket.handshake("127.0.0.1:18088", "/ws/sessions/" + session_id);

  beast::flat_buffer first_buffer;
  first_websocket.read(first_buffer);
  first_buffer.consume(first_buffer.size());
  first_websocket.read(first_buffer);
  first_buffer.consume(first_buffer.size());

  first_websocket.write(asio::buffer(std::string(R"({"type":"session.control.request"})")));
  first_websocket.read(first_buffer);
  const std::string remote_control_payload = beast::buffers_to_string(first_buffer.data());
  EXPECT_NE(remote_control_payload.find("\"controllerKind\":\"remote\""), std::string::npos);

  asio::io_context second_io_context;
  tcp::resolver second_resolver(second_io_context);
  websocket::stream<tcp::socket> second_websocket(second_io_context);
  const auto second_results = second_resolver.resolve("127.0.0.1", "18088");
  auto second_endpoint = asio::connect(second_websocket.next_layer(), second_results);
  static_cast<void>(second_endpoint);

  second_websocket.handshake("127.0.0.1:18088", "/ws/sessions/" + session_id);

  beast::flat_buffer second_buffer;
  second_websocket.read(second_buffer);
  second_buffer.consume(second_buffer.size());
  second_websocket.read(second_buffer);
  second_buffer.consume(second_buffer.size());

  second_websocket.write(asio::buffer(std::string(R"({"type":"session.control.request"})")));
  second_websocket.read(second_buffer);
  const std::string rejected_payload = beast::buffers_to_string(second_buffer.data());
  EXPECT_NE(rejected_payload.find("\"type\":\"error\""), std::string::npos);
  EXPECT_NE(rejected_payload.find("\"code\":\"command_rejected\""), std::string::npos);

  boost::system::error_code close_error;
  first_websocket.close(websocket::close_code::normal, close_error);
  EXPECT_FALSE(close_error);

  second_buffer.consume(second_buffer.size());
  second_websocket.read(second_buffer);
  const std::string host_release_payload = beast::buffers_to_string(second_buffer.data());
  EXPECT_NE(host_release_payload.find("\"type\":\"session.updated\""), std::string::npos);
  EXPECT_NE(host_release_payload.find("\"controllerKind\":\"host\""), std::string::npos);
  EXPECT_EQ(host_release_payload.find("\"controllerClientId\":"), std::string::npos);

  second_buffer.consume(second_buffer.size());
  second_websocket.write(asio::buffer(std::string(R"({"type":"session.control.request"})")));
  second_websocket.read(second_buffer);
  const std::string reacquired_payload = beast::buffers_to_string(second_buffer.data());
  EXPECT_NE(reacquired_payload.find("\"type\":\"session.updated\""), std::string::npos);
  EXPECT_NE(reacquired_payload.find("\"controllerKind\":\"remote\""), std::string::npos);
}

}  // namespace
}  // namespace vibe::net
