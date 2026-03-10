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
#include <optional>
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
    request.set(http::field::authorization, "Bearer token_p_1");
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

  static void StartPairing() {
    asio::io_context io_context;
    tcp::resolver resolver(io_context);
    tcp::socket socket(io_context);
    const auto results = resolver.resolve("127.0.0.1", "18088");
    asio::connect(socket, results);

    http::request<http::string_body> request{http::verb::post, "/pairing/request", 11};
    request.set(http::field::host, "127.0.0.1");
    request.set(http::field::content_type, "application/json");
    request.body() = "{\"deviceName\":\"integration-browser\",\"deviceType\":\"browser\"}";
    request.prepare_payload();

    http::write(socket, request);
    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(socket, buffer, response);
    EXPECT_EQ(response.result(), http::status::created);
  }

  static auto ApprovePairing() -> std::optional<std::string> {
    asio::io_context io_context;
    tcp::resolver resolver(io_context);
    tcp::socket socket(io_context);
    const auto results = resolver.resolve("127.0.0.1", "18088");
    asio::connect(socket, results);

    http::request<http::string_body> request{http::verb::post, "/pairing/approve", 11};
    request.set(http::field::host, "127.0.0.1");
    request.set(http::field::content_type, "application/json");
    request.body() = "{\"pairingId\":\"p_1\",\"code\":\"100001\"}";
    request.prepare_payload();

    http::write(socket, request);
    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(socket, buffer, response);
    if (response.result() != http::status::ok) {
      return std::nullopt;
    }

    const auto token_pos = response.body().find("\"token\":\"");
    if (token_pos == std::string::npos) {
      return std::nullopt;
    }

    const auto start = token_pos + std::string("\"token\":\"").size();
    const auto end = response.body().find('"', start);
    if (end == std::string::npos) {
      return std::nullopt;
    }

    return response.body().substr(start, end - start);
  }

  static auto EnsureApprovedToken() -> std::string {
    StartPairing();
    const auto token = ApprovePairing();
    EXPECT_TRUE(token.has_value());
    return token.value_or("");
  }

  std::atomic<bool> started_{false};
  HttpServer* server_{nullptr};
  std::thread server_thread_;
};

TEST_F(HttpServerFixture, WebSocketSessionEndpointStreamsTerminalOutput) {
  const std::string token = EnsureApprovedToken();
  const std::string create_response = CreateSession();
  ASSERT_NE(create_response.find("\"sessionId\":\"s_1\""), std::string::npos);

  asio::io_context io_context;
  tcp::resolver resolver(io_context);
  websocket::stream<tcp::socket> websocket(io_context);
  const auto results = resolver.resolve("127.0.0.1", "18088");
  auto endpoint = asio::connect(websocket.next_layer(), results);
  static_cast<void>(endpoint);

  websocket.set_option(websocket::stream_base::decorator(
      [&token](websocket::request_type& request) {
        request.set(http::field::authorization, "Bearer " + token);
      }));
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
  const std::string token = EnsureApprovedToken();
  const std::string create_response = CreateSession();
  ASSERT_NE(create_response.find("\"sessionId\":\"s_1\""), std::string::npos);

  asio::io_context io_context;
  tcp::resolver resolver(io_context);
  websocket::stream<tcp::socket> websocket(io_context);
  const auto results = resolver.resolve("127.0.0.1", "18088");
  auto endpoint = asio::connect(websocket.next_layer(), results);
  static_cast<void>(endpoint);

  websocket.set_option(websocket::stream_base::decorator(
      [&token](websocket::request_type& request) {
        request.set(http::field::authorization, "Bearer " + token);
      }));
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
  stop_request.set(http::field::authorization, "Bearer " + token);
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
  const std::string token = EnsureApprovedToken();
  const std::string create_response = CreateSession();
  ASSERT_NE(create_response.find("\"sessionId\":\"s_1\""), std::string::npos);

  asio::io_context io_context;
  tcp::resolver resolver(io_context);
  websocket::stream<tcp::socket> websocket(io_context);
  const auto results = resolver.resolve("127.0.0.1", "18088");
  auto endpoint = asio::connect(websocket.next_layer(), results);
  static_cast<void>(endpoint);

  websocket.set_option(websocket::stream_base::decorator(
      [&token](websocket::request_type& request) {
        request.set(http::field::authorization, "Bearer " + token);
      }));
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
  const std::string token = EnsureApprovedToken();
  const std::string create_response = CreateSession();
  ASSERT_NE(create_response.find("\"sessionId\":\"s_1\""), std::string::npos);

  asio::io_context io_context;
  tcp::resolver resolver(io_context);
  websocket::stream<tcp::socket> websocket(io_context);
  const auto results = resolver.resolve("127.0.0.1", "18088");
  auto endpoint = asio::connect(websocket.next_layer(), results);
  static_cast<void>(endpoint);

  websocket.set_option(websocket::stream_base::decorator(
      [&token](websocket::request_type& request) {
        request.set(http::field::authorization, "Bearer " + token);
      }));
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
  const std::string token = EnsureApprovedToken();
  const std::string create_response = CreateSession();
  ASSERT_NE(create_response.find("\"sessionId\":\"s_1\""), std::string::npos);

  asio::io_context io_context;
  tcp::resolver resolver(io_context);
  websocket::stream<tcp::socket> websocket(io_context);
  const auto results = resolver.resolve("127.0.0.1", "18088");
  auto endpoint = asio::connect(websocket.next_layer(), results);
  static_cast<void>(endpoint);

  websocket.set_option(websocket::stream_base::decorator(
      [&token](websocket::request_type& request) {
        request.set(http::field::authorization, "Bearer " + token);
      }));
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
  const std::string token = EnsureApprovedToken();
  const std::string create_response = CreateSession();
  ASSERT_NE(create_response.find("\"sessionId\":\"s_1\""), std::string::npos);

  asio::io_context io_context;
  tcp::resolver resolver(io_context);
  websocket::stream<tcp::socket> websocket(io_context);
  const auto results = resolver.resolve("127.0.0.1", "18088");
  auto endpoint = asio::connect(websocket.next_layer(), results);
  static_cast<void>(endpoint);

  websocket.set_option(websocket::stream_base::decorator(
      [&token](websocket::request_type& request) {
        request.set(http::field::authorization, "Bearer " + token);
      }));
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

TEST_F(HttpServerFixture, SessionRoutesRejectMissingBearerToken) {
  asio::io_context io_context;
  tcp::resolver resolver(io_context);
  tcp::socket socket(io_context);
  const auto results = resolver.resolve("127.0.0.1", "18088");
  asio::connect(socket, results);

  http::request<http::string_body> request{http::verb::get, "/sessions", 11};
  request.set(http::field::host, "127.0.0.1");
  http::write(socket, request);

  beast::flat_buffer buffer;
  http::response<http::string_body> response;
  http::read(socket, buffer, response);
  EXPECT_EQ(response.result(), http::status::unauthorized);
}

TEST_F(HttpServerFixture, WebSocketSessionEndpointRejectsMissingBearerToken) {
  const std::string token = EnsureApprovedToken();
  const std::string create_response = CreateSession();
  ASSERT_NE(create_response.find("\"sessionId\":\"s_1\""), std::string::npos);

  asio::io_context io_context;
  tcp::resolver resolver(io_context);
  websocket::stream<tcp::socket> websocket(io_context);
  const auto results = resolver.resolve("127.0.0.1", "18088");
  auto endpoint = asio::connect(websocket.next_layer(), results);
  static_cast<void>(endpoint);

  boost::system::system_error error(http::error::end_of_stream);
  try {
    websocket.handshake("127.0.0.1:18088", "/ws/sessions/s_1");
  } catch (const boost::system::system_error& exception) {
    error = exception;
  }

  EXPECT_EQ(error.code(), websocket::error::upgrade_declined);
}

TEST_F(HttpServerFixture, LocalUiEndpointsSupportPairingAndConfig) {
  StartPairing();

  asio::io_context io_context;
  tcp::resolver resolver(io_context);
  tcp::socket socket(io_context);
  const auto results = resolver.resolve("127.0.0.1", "18088");
  asio::connect(socket, results);

  http::request<http::string_body> pending_request{http::verb::get, "/pairing/pending", 11};
  pending_request.set(http::field::host, "127.0.0.1");
  http::write(socket, pending_request);

  beast::flat_buffer pending_buffer;
  http::response<http::string_body> pending_response;
  http::read(socket, pending_buffer, pending_response);
  EXPECT_EQ(pending_response.result(), http::status::ok);
  EXPECT_NE(pending_response.body().find("\"pairingId\":\"p_1\""), std::string::npos);

  boost::system::error_code error_code;
  socket.shutdown(tcp::socket::shutdown_both, error_code);

  asio::io_context config_io_context;
  tcp::resolver config_resolver(config_io_context);
  tcp::socket config_socket(config_io_context);
  const auto config_results = config_resolver.resolve("127.0.0.1", "18088");
  asio::connect(config_socket, config_results);

  http::request<http::string_body> config_request{http::verb::post, "/host/config", 11};
  config_request.set(http::field::host, "127.0.0.1");
  config_request.set(http::field::content_type, "application/json");
  config_request.body() = R"({"displayName":"Updated Host"})";
  config_request.prepare_payload();
  http::write(config_socket, config_request);

  beast::flat_buffer config_buffer;
  http::response<http::string_body> config_response;
  http::read(config_socket, config_buffer, config_response);
  EXPECT_EQ(config_response.result(), http::status::ok);
  EXPECT_NE(config_response.body().find("\"displayName\":\"Updated Host\""), std::string::npos);
}

}  // namespace
}  // namespace vibe::net
