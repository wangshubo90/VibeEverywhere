#include <gtest/gtest.h>

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/json.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>

#include "vibe/net/http_server.h"

namespace vibe::net {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace json = boost::json;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;
namespace http = beast::http;

class HttpServerFixture : public ::testing::Test {
 protected:
  static constexpr std::uint16_t kRemotePort = 18088;
  static constexpr std::uint16_t kAdminPort = 18089;

  void StartServer() {
    started_.store(false);
    server_thread_ = std::thread([this]() {
      HttpServer server("127.0.0.1", kAdminPort, "127.0.0.1", kRemotePort, storage_root_);
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

  void StopServer() {
    if (server_ != nullptr) {
      server_->Stop();
    }
    if (server_thread_.joinable()) {
      server_thread_.join();
    }
  }

  void SetUp() override {
    storage_root_ = std::filesystem::temp_directory_path() /
                    ("vibe-net-test-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(storage_root_);
    std::filesystem::create_directories(storage_root_);
    StartServer();
  }

  void TearDown() override {
    StopServer();
    std::filesystem::remove_all(storage_root_);
  }

  static auto ReadJsonResponseBody(http::response<http::string_body>& response) -> json::object {
    boost::system::error_code error_code;
    auto parsed = json::parse(response.body(), error_code);
    EXPECT_FALSE(error_code);
    EXPECT_TRUE(parsed.is_object());
    return parsed.as_object();
  }

  static auto SendRequest(const std::uint16_t port, const http::verb method, const std::string& target,
                          const std::string& body = "",
                          const std::optional<std::string>& bearer_token = std::nullopt)
      -> http::response<http::string_body> {
    asio::io_context io_context;
    tcp::resolver resolver(io_context);
    tcp::socket socket(io_context);
    const auto results = resolver.resolve("127.0.0.1", std::to_string(port));
    asio::connect(socket, results);

    http::request<http::string_body> request{method, target, 11};
    request.set(http::field::host, "127.0.0.1");
    if (bearer_token.has_value()) {
      request.set(http::field::authorization, "Bearer " + *bearer_token);
    }
    if (!body.empty()) {
      request.set(http::field::content_type, "application/json");
      request.body() = body;
      request.prepare_payload();
    }

    http::write(socket, request);
    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(socket, buffer, response);
    boost::system::error_code error_code;
    socket.shutdown(tcp::socket::shutdown_both, error_code);
    return response;
  }

  static auto CreateSession(const std::string& token) -> std::string {
    auto response = SendRequest(kRemotePort, http::verb::post, "/sessions",
                                "{\"provider\":\"codex\",\"workspaceRoot\":\".\",\"title\":\"ws-session\"}",
                                token);
    EXPECT_EQ(response.result(), http::status::created);
    return response.body();
  }

  static auto StartPairing() -> std::optional<json::object> {
    auto response = SendRequest(kRemotePort, http::verb::post, "/pairing/request",
                                "{\"deviceName\":\"integration-browser\",\"deviceType\":\"browser\"}");
    if (response.result() != http::status::created) {
      return std::nullopt;
    }

    return ReadJsonResponseBody(response);
  }

  static auto ApprovePairing(const std::string& pairing_id, const std::string& code)
      -> std::optional<std::string> {
    auto response = SendRequest(kAdminPort, http::verb::post, "/pairing/approve",
                                "{\"pairingId\":\"" + pairing_id + "\",\"code\":\"" + code + "\"}");
    if (response.result() != http::status::ok) {
      return std::nullopt;
    }

    const auto body = ReadJsonResponseBody(response);
    const auto token = body.if_contains("token");
    if (token == nullptr || !token->is_string()) {
      return std::nullopt;
    }

    return json::value_to<std::string>(*token);
  }

  static auto EnsureApprovedToken() -> std::string {
    const auto pairing = StartPairing();
    if (!pairing.has_value()) {
      ADD_FAILURE() << "pairing request failed";
      return "";
    }

    const auto pairing_id = pairing->if_contains("pairingId");
    const auto code = pairing->if_contains("code");
    if (pairing_id == nullptr || code == nullptr || !pairing_id->is_string() || !code->is_string()) {
      ADD_FAILURE() << "pairing response missing id/code";
      return "";
    }

    const auto token =
        ApprovePairing(json::value_to<std::string>(*pairing_id), json::value_to<std::string>(*code));
    if (!token.has_value()) {
      ADD_FAILURE() << "pairing approval failed";
      return "";
    }
    return *token;
  }

  std::atomic<bool> started_{false};
  HttpServer* server_{nullptr};
  std::thread server_thread_;
  std::filesystem::path storage_root_;
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
  const std::string token = EnsureApprovedToken();
  const std::string create_response = CreateSession(token);
  const std::string session_id = ExtractSessionId(create_response);
  ASSERT_FALSE(session_id.empty());

  asio::io_context io_context;
  tcp::resolver resolver(io_context);
  websocket::stream<tcp::socket> websocket(io_context);
  const auto results = resolver.resolve("127.0.0.1", std::to_string(kRemotePort));
  auto endpoint = asio::connect(websocket.next_layer(), results);
  static_cast<void>(endpoint);

  websocket.set_option(websocket::stream_base::decorator(
      [&token](websocket::request_type& request) {
        request.set(http::field::authorization, "Bearer " + token);
      }));
  websocket.handshake("127.0.0.1:" + std::to_string(kRemotePort), "/ws/sessions/" + session_id);

  beast::flat_buffer buffer;
  websocket.read(buffer);
  const std::string first_payload = beast::buffers_to_string(buffer.data());
  EXPECT_NE(first_payload.find("\"type\":\"session.updated\""), std::string::npos);
  EXPECT_NE(first_payload.find("\"sessionId\":\"" + session_id + "\""), std::string::npos);
  EXPECT_NE(first_payload.find("\"status\""), std::string::npos);
  EXPECT_NE(first_payload.find("\"controllerKind\":\"host\""), std::string::npos);
  EXPECT_NE(first_payload.find("\"activityState\":\"active\""), std::string::npos);
  EXPECT_NE(first_payload.find("\"isRecovered\":false"), std::string::npos);

  buffer.consume(buffer.size());
  websocket.read(buffer);
  const std::string second_payload = beast::buffers_to_string(buffer.data());
  EXPECT_NE(second_payload.find("\"type\":\"terminal.output\""), std::string::npos);
  EXPECT_NE(second_payload.find("\"sessionId\":\"" + session_id + "\""), std::string::npos);
  EXPECT_NE(second_payload.find("\"seqStart\""), std::string::npos);
  EXPECT_NE(second_payload.find("\"dataEncoding\":\"base64\""), std::string::npos);
  EXPECT_NE(second_payload.find("\"dataBase64\""), std::string::npos);
}

TEST_F(HttpServerFixture, WebSocketSessionEndpointStreamsExitEventsAfterStop) {
  const std::string token = EnsureApprovedToken();
  const std::string create_response = CreateSession(token);
  const std::string session_id = ExtractSessionId(create_response);
  ASSERT_FALSE(session_id.empty());

  asio::io_context io_context;
  tcp::resolver resolver(io_context);
  websocket::stream<tcp::socket> websocket(io_context);
  const auto results = resolver.resolve("127.0.0.1", std::to_string(kRemotePort));
  auto endpoint = asio::connect(websocket.next_layer(), results);
  static_cast<void>(endpoint);

  websocket.set_option(websocket::stream_base::decorator(
      [&token](websocket::request_type& request) {
        request.set(http::field::authorization, "Bearer " + token);
      }));
  websocket.handshake("127.0.0.1:" + std::to_string(kRemotePort), "/ws/sessions/" + session_id);

  beast::flat_buffer buffer;
  websocket.read(buffer);
  buffer.consume(buffer.size());
  websocket.read(buffer);
  buffer.consume(buffer.size());

  auto stop_response = SendRequest(kRemotePort, http::verb::post, "/sessions/" + session_id + "/stop", "", token);
  EXPECT_EQ(stop_response.result(), http::status::ok);

  websocket.read(buffer);
  const std::string updated_payload = beast::buffers_to_string(buffer.data());
  EXPECT_NE(updated_payload.find("\"type\":\"session.updated\""), std::string::npos);
  EXPECT_NE(updated_payload.find("\"status\":\"Exited\""), std::string::npos);
  EXPECT_NE(updated_payload.find("\"activityState\":\"stopped\""), std::string::npos);

  buffer.consume(buffer.size());
  websocket.read(buffer);
  const std::string exited_payload = beast::buffers_to_string(buffer.data());
  EXPECT_NE(exited_payload.find("\"type\":\"session.exited\""), std::string::npos);
  EXPECT_NE(exited_payload.find("\"sessionId\":\"" + session_id + "\""), std::string::npos);
  EXPECT_NE(exited_payload.find("\"status\":\"Exited\""), std::string::npos);
}

TEST_F(HttpServerFixture, WebSocketSessionEndpointRejectsInvalidCommands) {
  const std::string token = EnsureApprovedToken();
  const std::string create_response = CreateSession(token);
  const std::string session_id = ExtractSessionId(create_response);
  ASSERT_FALSE(session_id.empty());

  asio::io_context io_context;
  tcp::resolver resolver(io_context);
  websocket::stream<tcp::socket> websocket(io_context);
  const auto results = resolver.resolve("127.0.0.1", std::to_string(kRemotePort));
  auto endpoint = asio::connect(websocket.next_layer(), results);
  static_cast<void>(endpoint);

  websocket.set_option(websocket::stream_base::decorator(
      [&token](websocket::request_type& request) {
        request.set(http::field::authorization, "Bearer " + token);
      }));
  websocket.handshake("127.0.0.1:" + std::to_string(kRemotePort), "/ws/sessions/" + session_id);

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
  const std::string create_response = CreateSession(token);
  const std::string session_id = ExtractSessionId(create_response);
  ASSERT_FALSE(session_id.empty());

  asio::io_context io_context;
  tcp::resolver resolver(io_context);
  websocket::stream<tcp::socket> websocket(io_context);
  const auto results = resolver.resolve("127.0.0.1", std::to_string(kRemotePort));
  auto endpoint = asio::connect(websocket.next_layer(), results);
  static_cast<void>(endpoint);

  websocket.set_option(websocket::stream_base::decorator(
      [&token](websocket::request_type& request) {
        request.set(http::field::authorization, "Bearer " + token);
      }));
  websocket.handshake("127.0.0.1:" + std::to_string(kRemotePort), "/ws/sessions/" + session_id);

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
  const std::string create_response = CreateSession(token);
  const std::string session_id = ExtractSessionId(create_response);
  ASSERT_FALSE(session_id.empty());

  asio::io_context io_context;
  tcp::resolver resolver(io_context);
  websocket::stream<tcp::socket> websocket(io_context);
  const auto results = resolver.resolve("127.0.0.1", std::to_string(kRemotePort));
  auto endpoint = asio::connect(websocket.next_layer(), results);
  static_cast<void>(endpoint);

  websocket.set_option(websocket::stream_base::decorator(
      [&token](websocket::request_type& request) {
        request.set(http::field::authorization, "Bearer " + token);
      }));
  websocket.handshake("127.0.0.1:" + std::to_string(kRemotePort), "/ws/sessions/" + session_id);

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
  const std::string create_response = CreateSession(token);
  const std::string session_id = ExtractSessionId(create_response);
  ASSERT_FALSE(session_id.empty());

  asio::io_context io_context;
  tcp::resolver resolver(io_context);
  websocket::stream<tcp::socket> websocket(io_context);
  const auto results = resolver.resolve("127.0.0.1", std::to_string(kRemotePort));
  auto endpoint = asio::connect(websocket.next_layer(), results);
  static_cast<void>(endpoint);

  websocket.set_option(websocket::stream_base::decorator(
      [&token](websocket::request_type& request) {
        request.set(http::field::authorization, "Bearer " + token);
      }));
  websocket.handshake("127.0.0.1:" + std::to_string(kRemotePort), "/ws/sessions/" + session_id);

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
  const auto results = resolver.resolve("127.0.0.1", std::to_string(kRemotePort));
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
  const std::string create_response = CreateSession(token);
  const std::string session_id = ExtractSessionId(create_response);
  ASSERT_FALSE(session_id.empty());

  asio::io_context io_context;
  tcp::resolver resolver(io_context);
  websocket::stream<tcp::socket> websocket(io_context);
  const auto results = resolver.resolve("127.0.0.1", std::to_string(kRemotePort));
  auto endpoint = asio::connect(websocket.next_layer(), results);
  static_cast<void>(endpoint);

  boost::system::system_error error(http::error::end_of_stream);
  try {
    websocket.handshake("127.0.0.1:" + std::to_string(kRemotePort), "/ws/sessions/" + session_id);
  } catch (const boost::system::system_error& exception) {
    error = exception;
  }

  EXPECT_EQ(error.code(), websocket::error::upgrade_declined);
}

TEST_F(HttpServerFixture, WebSocketSessionEndpointAcceptsAccessTokenQueryParameter) {
  const std::string token = EnsureApprovedToken();
  const std::string create_response = CreateSession(token);
  const std::string session_id = ExtractSessionId(create_response);
  ASSERT_FALSE(session_id.empty());

  asio::io_context io_context;
  tcp::resolver resolver(io_context);
  websocket::stream<tcp::socket> websocket(io_context);
  const auto results = resolver.resolve("127.0.0.1", std::to_string(kRemotePort));
  auto endpoint = asio::connect(websocket.next_layer(), results);
  static_cast<void>(endpoint);

  websocket.handshake("127.0.0.1:" + std::to_string(kRemotePort),
                      "/ws/sessions/" + session_id + "?access_token=" + token);

  beast::flat_buffer buffer;
  websocket.read(buffer);
  const std::string payload = beast::buffers_to_string(buffer.data());
  EXPECT_NE(payload.find("\"type\":\"session.updated\""), std::string::npos);
  EXPECT_NE(payload.find("\"sessionId\":\"" + session_id + "\""), std::string::npos);
}

TEST_F(HttpServerFixture, LocalUiEndpointsSupportPairingAndConfig) {
  const auto pairing = StartPairing();
  ASSERT_TRUE(pairing.has_value());

  auto pending_response = SendRequest(kAdminPort, http::verb::get, "/pairing/pending");
  EXPECT_EQ(pending_response.result(), http::status::ok);
  EXPECT_NE(pending_response.body().find("\"pairingId\":\""), std::string::npos);

  auto config_response =
      SendRequest(kAdminPort, http::verb::post, "/host/config", R"({"displayName":"Updated Host"})");
  EXPECT_EQ(config_response.result(), http::status::ok);
  EXPECT_NE(config_response.body().find("\"displayName\":\"Updated Host\""), std::string::npos);
}

TEST_F(HttpServerFixture, PairingAndHostConfigPersistAcrossServerRestart) {
  const auto pairing = StartPairing();
  ASSERT_TRUE(pairing.has_value());
  const auto pairing_id = pairing->if_contains("pairingId");
  const auto code = pairing->if_contains("code");
  ASSERT_NE(pairing_id, nullptr);
  ASSERT_NE(code, nullptr);
  ASSERT_TRUE(pairing_id->is_string());
  ASSERT_TRUE(code->is_string());

  auto config_response =
      SendRequest(kAdminPort, http::verb::post, "/host/config", R"({"displayName":"Persistent Host"})");
  ASSERT_EQ(config_response.result(), http::status::ok);

  StopServer();
  StartServer();

  auto pending_response = SendRequest(kAdminPort, http::verb::get, "/pairing/pending");
  ASSERT_EQ(pending_response.result(), http::status::ok);
  EXPECT_NE(pending_response.body().find(json::value_to<std::string>(*pairing_id)), std::string::npos);

  auto host_info_response = SendRequest(kAdminPort, http::verb::get, "/host/info");
  ASSERT_EQ(host_info_response.result(), http::status::ok);
  EXPECT_NE(host_info_response.body().find("\"displayName\":\"Persistent Host\""), std::string::npos);

  const auto token =
      ApprovePairing(json::value_to<std::string>(*pairing_id), json::value_to<std::string>(*code));
  ASSERT_TRUE(token.has_value());

  auto sessions_response = SendRequest(kRemotePort, http::verb::get, "/sessions", "", *token);
  EXPECT_EQ(sessions_response.result(), http::status::ok);
}

TEST_F(HttpServerFixture, HostDetachClearsStaleHostControllerClaim) {
  const std::string token = EnsureApprovedToken();
  const std::string create_response = CreateSession(token);
  const std::string session_id = ExtractSessionId(create_response);
  ASSERT_FALSE(session_id.empty());

  asio::io_context first_io_context;
  tcp::resolver first_resolver(first_io_context);
  websocket::stream<tcp::socket> first_websocket(first_io_context);
  const auto first_results = first_resolver.resolve("127.0.0.1", std::to_string(kRemotePort));
  auto first_endpoint = asio::connect(first_websocket.next_layer(), first_results);
  static_cast<void>(first_endpoint);

  first_websocket.set_option(websocket::stream_base::decorator(
      [&token](websocket::request_type& request) {
        request.set(http::field::authorization, "Bearer " + token);
      }));
  first_websocket.handshake("127.0.0.1:" + std::to_string(kRemotePort), "/ws/sessions/" + session_id);

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
  const auto second_results = second_resolver.resolve("127.0.0.1", std::to_string(kRemotePort));
  auto second_endpoint = asio::connect(second_websocket.next_layer(), second_results);
  static_cast<void>(second_endpoint);

  second_websocket.set_option(websocket::stream_base::decorator(
      [&token](websocket::request_type& request) {
        request.set(http::field::authorization, "Bearer " + token);
      }));
  second_websocket.handshake("127.0.0.1:" + std::to_string(kRemotePort), "/ws/sessions/" + session_id);

  beast::flat_buffer second_buffer;
  second_websocket.read(second_buffer);
  const std::string updated_payload = beast::buffers_to_string(second_buffer.data());
  EXPECT_NE(updated_payload.find("\"type\":\"session.updated\""), std::string::npos);
  EXPECT_NE(updated_payload.find("\"controllerKind\":\"host\""), std::string::npos);
  EXPECT_EQ(updated_payload.find("\"controllerClientId\":"), std::string::npos);
}

TEST_F(HttpServerFixture, RemoteControlReturnsToHostAfterControllerDisconnects) {
  const std::string token = EnsureApprovedToken();
  const std::string create_response = CreateSession(token);
  const std::string session_id = ExtractSessionId(create_response);
  ASSERT_FALSE(session_id.empty());

  asio::io_context first_io_context;
  tcp::resolver first_resolver(first_io_context);
  websocket::stream<tcp::socket> first_websocket(first_io_context);
  const auto first_results = first_resolver.resolve("127.0.0.1", std::to_string(kRemotePort));
  auto first_endpoint = asio::connect(first_websocket.next_layer(), first_results);
  static_cast<void>(first_endpoint);

  first_websocket.set_option(websocket::stream_base::decorator(
      [&token](websocket::request_type& request) {
        request.set(http::field::authorization, "Bearer " + token);
      }));
  first_websocket.handshake("127.0.0.1:" + std::to_string(kRemotePort), "/ws/sessions/" + session_id);

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
  const auto second_results = second_resolver.resolve("127.0.0.1", std::to_string(kRemotePort));
  auto second_endpoint = asio::connect(second_websocket.next_layer(), second_results);
  static_cast<void>(second_endpoint);

  second_websocket.set_option(websocket::stream_base::decorator(
      [&token](websocket::request_type& request) {
        request.set(http::field::authorization, "Bearer " + token);
      }));
  second_websocket.handshake("127.0.0.1:" + std::to_string(kRemotePort), "/ws/sessions/" + session_id);

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
