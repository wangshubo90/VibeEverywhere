#include <gtest/gtest.h>

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/json.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#include <unistd.h>

#include "vibe/net/http_server.h"

namespace vibe::net {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace json = boost::json;
namespace ssl = asio::ssl;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;
namespace http = beast::http;

constexpr std::string_view kTestCertificatePem =
    "-----BEGIN CERTIFICATE-----\n"
    "MIICpDCCAYwCCQDq0rZDpM4KYTANBgkqhkiG9w0BAQsFADAUMRIwEAYDVQQDDAkx\n"
    "MjcuMC4wLjEwHhcNMjYwMzEwMjAzNDAwWhcNMjYwMzExMjAzNDAwWjAUMRIwEAYD\n"
    "VQQDDAkxMjcuMC4wLjEwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQCl\n"
    "iwaZOwgPTcf+eRpY9Qx7rdfgWmjMA0mBAR+PJl+5PA0aQfloM0gV0gC0WWRzz23X\n"
    "QN3219X2S2GFcjsJd2zElRMGxIYBNDexKf8KZBJLX2tvCr4//6afWcLIICZFaD0X\n"
    "hZRhM7zJdA9hHeCNMAGSAxxOhf+9eFct6yY+gzy/IzG3TDtj6V7MFJNjBlzDRVuw\n"
    "aDjbP0NX2tame44FiJqU1kauu2Yvs1JZPGRVE79HY0NPzIswIRjgx7PP/mTdKeEj\n"
    "ALrFbG79Li0Q2bOkCGOlM2XfjIw/+rYZ/6PRM6pdOO0318RZ5P+5uF0BeuZBVNO7\n"
    "9hFsyVolbA0WoSxkdqspAgMBAAEwDQYJKoZIhvcNAQELBQADggEBAKUZ8s8/RiKe\n"
    "70Biw39RErhC3112iSBS5TJayK/K0MqZtGPkcetxtZJgAyNYvpsMGH4/5RDlqhBL\n"
    "efKm3Zdn3ifP6zizNJHTIGgoo6vSLXt3ZHSW+C//T6ou5o25juiwle/9wDARZ9TF\n"
    "Sa1e+VmqvVy0p/QrYqW8JC5dFSra6rFPCPTimiZKAJ9cbIucfTRDn0e1n9t3Xros\n"
    "hTTp8p7CJtZLnk1O/yHLBRJHQq9NcfuKsglSgSAQnilPj3TO/FAPapFhtpfasJfw\n"
    "iPKQ1f8NPYVAOcRNp+kXwo/ZmhXKaIzQZ38ktRTiEOiYdQYRsvKbYEBjLUb/P3w3\n"
    "0exsODbQjgY=\n"
    "-----END CERTIFICATE-----\n";

constexpr std::string_view kTestPrivateKeyPem =
    "-----BEGIN PRIVATE KEY-----\n"
    "MIIEvAIBADANBgkqhkiG9w0BAQEFAASCBKYwggSiAgEAAoIBAQCliwaZOwgPTcf+\n"
    "eRpY9Qx7rdfgWmjMA0mBAR+PJl+5PA0aQfloM0gV0gC0WWRzz23XQN3219X2S2GF\n"
    "cjsJd2zElRMGxIYBNDexKf8KZBJLX2tvCr4//6afWcLIICZFaD0XhZRhM7zJdA9h\n"
    "HeCNMAGSAxxOhf+9eFct6yY+gzy/IzG3TDtj6V7MFJNjBlzDRVuwaDjbP0NX2tam\n"
    "e44FiJqU1kauu2Yvs1JZPGRVE79HY0NPzIswIRjgx7PP/mTdKeEjALrFbG79Li0Q\n"
    "2bOkCGOlM2XfjIw/+rYZ/6PRM6pdOO0318RZ5P+5uF0BeuZBVNO79hFsyVolbA0W\n"
    "oSxkdqspAgMBAAECggEADRrIVvD5XlzF7Dsh/tDHQDMu16/Qg/Xup6uzF93tzfgX\n"
    "AXEfkUQd/Lw3Gg/Jh/FvvI6CA7xqT6j/+1RjoAg4MAetDegYw5eioP+2FNh/KkBP\n"
    "fObY0LAoPI0agkUpQm1SCL6mVFuIIlpxrTp0QDkAptJgN0ccS59vJ09qI6C3+llq\n"
    "SfUOGj0aYgQWbo2UGXAH6p9EfQtnVg28vIriSMJhqcQVShdQYpEoCx+a2mm/AIUd\n"
    "PME8wNIztKg7S921MGxNsT6iwOsYxEjPMYohkj+lUzAcc74UWi7Ago/BEfaGOaOb\n"
    "emj9cG42olj6fvJuhhGArpfPs7RwDE6fkmzwJVDkAQKBgQDPrjqvoSsBWcIVmEZZ\n"
    "NqZUI1wwcMH5x2KYOYGO5/dssi0CELYiZJQtchTQO25Tzd5J6YBGtTaPnMJEHhSv\n"
    "AdK1/NodVm1UXe8gX9+ED/nHL0tbXYn0pD+REnOysHnrP3LtHchpz2YnNKSl5YNE\n"
    "afokzeCpnvTd6c4hDW0oeEhpCQKBgQDMDwWzRXKKfgsEqytBxag9Te0WnKD+fRLZ\n"
    "2PYbW7VgmrqvGCsKEOroSz4t9LTxYgaQsl0q6q8ZaIoTtjSpg6F506FicHv2Izb7\n"
    "mdGR2qiWZj4dqSndqi8SBC2c9beQ6/GQ4MwOFdylkYft+/I6+3kkKwybioRcnZUL\n"
    "wxvO6fVZIQKBgENVObG3jzng4AwgUq6aCVM+I6WQ6VMSUeUIv/iVPKMBIZaJ1INS\n"
    "GLijRBw/KIYDOQ69pdlG52moaVOsbQhQjwBx+kWIv+maiuz0KIOoqlLqAoSczx2C\n"
    "Ixnl4Z0NmnmrGJfIEDI+C+CqMLeYsfZ8ZZ2JIk3cO4e3Rh1xPPLiDJgBAoGAbNP2\n"
    "Lk3zcHkYVz0SrNlFiKxurYiLNC5wR062spCOgx8CQS+ahJvRLGI0nz3K4pFs/u6h\n"
    "UaooCF0AhtK980oIbHi5sU5cTkEpqbW3VxpOnyeYKSl28ok44VrpGLTTWa77/lBl\n"
    "g32VV5ft7rZX0a0cRnXPG4UcYmiIfOVph0ivWQECgYBLgqFxDA34XhH7yhGxGYLB\n"
    "F/c3ggSV51x7len0l1GQPRZhJ4yRGhm5JkXAslg8qhY63cE3HAiYH3Xkwhae2fE7\n"
    "gcj5VeYbcX2ThNIDBdG4X3NIDVtcCZ0WogLN3mPLwzeqsNZPrOoVpt+yOTh3vxXw\n"
    "UZeHqLTTIsdkEycZb2Usmw==\n"
    "-----END PRIVATE KEY-----\n";

class HttpServerFixture : public ::testing::Test {
 protected:
  void StartServer(std::optional<RemoteTlsFiles> remote_tls_override = std::nullopt) {
    started_.store(false);
    run_finished_.store(false);
    run_result_.store(false);
    remote_tls_override_ = std::move(remote_tls_override);
    server_thread_ = std::thread([this]() {
      HttpServer server("127.0.0.1", kAdminPort, "127.0.0.1", kRemotePort, storage_root_,
                        remote_tls_override_);
      server_ = &server;
      started_.store(true);
      const bool ran = server.Run();
      run_result_.store(ran);
      run_finished_.store(true);
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
    const auto pid_component = static_cast<std::uint16_t>(::getpid() % 10000);
    const auto time_component = static_cast<std::uint16_t>(
        std::chrono::steady_clock::now().time_since_epoch().count() % 1000);
    kRemotePort = static_cast<std::uint16_t>(20000 + ((pid_component * 2U + time_component * 2U) % 20000));
    kAdminPort = static_cast<std::uint16_t>(kRemotePort + 1U);
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

  auto ReadJsonResponseBody(http::response<http::string_body>& response) -> json::object {
    boost::system::error_code error_code;
    auto parsed = json::parse(response.body(), error_code);
    EXPECT_FALSE(error_code);
    EXPECT_TRUE(parsed.is_object());
    return parsed.as_object();
  }

  auto SendRequest(const std::uint16_t port, const http::verb method, const std::string& target,
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

  auto SendSecureRequest(const std::uint16_t port, const http::verb method, const std::string& target,
                         const std::string& body = "",
                         const std::optional<std::string>& bearer_token = std::nullopt)
      -> http::response<http::string_body> {
    asio::io_context io_context;
    ssl::context ssl_context(ssl::context::tls_client);
    ssl_context.set_verify_mode(ssl::verify_none);
    tcp::resolver resolver(io_context);
    beast::ssl_stream<tcp::socket> stream(io_context, ssl_context);
    const auto results = resolver.resolve("127.0.0.1", std::to_string(port));
    asio::connect(stream.next_layer(), results);
    stream.handshake(ssl::stream_base::client);

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

    http::write(stream, request);
    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(stream, buffer, response);
    boost::system::error_code error_code;
    stream.shutdown(error_code);
    return response;
  }

  auto CreateSession(const std::string& token) -> std::string {
    auto response = SendRequest(kRemotePort, http::verb::post, "/sessions",
                                "{\"provider\":\"codex\",\"workspaceRoot\":\".\",\"title\":\"ws-session\"}",
                                token);
    EXPECT_EQ(response.result(), http::status::created);
    return response.body();
  }

  auto StartPairing() -> std::optional<json::object> {
    auto response = SendRequest(kRemotePort, http::verb::post, "/pairing/request",
                                "{\"deviceName\":\"integration-browser\",\"deviceType\":\"browser\"}");
    if (response.result() != http::status::created) {
      return std::nullopt;
    }

    return ReadJsonResponseBody(response);
  }

  auto StartSecurePairing() -> std::optional<json::object> {
    auto response = SendSecureRequest(kRemotePort, http::verb::post, "/pairing/request",
                                      "{\"deviceName\":\"integration-browser\",\"deviceType\":\"browser\"}");
    if (response.result() != http::status::created) {
      return std::nullopt;
    }

    return ReadJsonResponseBody(response);
  }

  auto ApprovePairing(const std::string& pairing_id, const std::string& code)
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

  auto EnsureApprovedToken() -> std::string {
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

  auto EnsureApprovedSecureToken() -> std::string {
    const auto pairing = StartSecurePairing();
    if (!pairing.has_value()) {
      ADD_FAILURE() << "secure pairing request failed";
      return "";
    }

    const auto pairing_id = pairing->if_contains("pairingId");
    const auto code = pairing->if_contains("code");
    if (pairing_id == nullptr || code == nullptr || !pairing_id->is_string() || !code->is_string()) {
      ADD_FAILURE() << "secure pairing response missing id/code";
      return "";
    }

    const auto token =
        ApprovePairing(json::value_to<std::string>(*pairing_id), json::value_to<std::string>(*code));
    if (!token.has_value()) {
      ADD_FAILURE() << "secure pairing approval failed";
      return "";
    }
    return *token;
  }

  auto CreateSecureSession(const std::string& token) -> std::string {
    auto response = SendSecureRequest(kRemotePort, http::verb::post, "/sessions",
                                      "{\"provider\":\"codex\",\"workspaceRoot\":\".\",\"title\":\"ws-session\"}",
                                      token);
    EXPECT_EQ(response.result(), http::status::created);
    return response.body();
  }

  void WriteTextFile(const std::filesystem::path& path, const std::string_view content) const {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.is_open());
    output << content;
    ASSERT_TRUE(output.good());
  }

  void WriteHostIdentity(const std::string& certificate_path, const std::string& key_path) const {
    WriteTextFile(storage_root_ / "host_identity.json",
                  std::string("{\"hostId\":\"host_1\",\"displayName\":\"Dev Host\",\"certificatePemPath\":\"") +
                      certificate_path + "\",\"privateKeyPemPath\":\"" + key_path + "\"}");
  }

  void EnablePersistedRemoteTls() {
    const auto certificate_path = storage_root_ / "remote-cert.pem";
    const auto key_path = storage_root_ / "remote-key.pem";
    WriteTextFile(certificate_path, kTestCertificatePem);
    WriteTextFile(key_path, kTestPrivateKeyPem);
    WriteHostIdentity(certificate_path.string(), key_path.string());
  }

  std::atomic<bool> started_{false};
  std::atomic<bool> run_finished_{false};
  std::atomic<bool> run_result_{false};
  HttpServer* server_{nullptr};
  std::thread server_thread_;
  std::filesystem::path storage_root_;
  std::optional<RemoteTlsFiles> remote_tls_override_{std::nullopt};
  std::uint16_t kRemotePort{0};
  std::uint16_t kAdminPort{0};
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
  ASSERT_NE(create_response.find("\"sessionId\":\"s_1\""), std::string::npos);

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
  websocket.handshake("127.0.0.1:" + std::to_string(kRemotePort), "/ws/sessions/s_1");

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
  const std::string create_response = CreateSession(token);
  ASSERT_NE(create_response.find("\"sessionId\":\"s_1\""), std::string::npos);

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
  websocket.handshake("127.0.0.1:" + std::to_string(kRemotePort), "/ws/sessions/s_1");

  beast::flat_buffer buffer;
  websocket.read(buffer);
  buffer.consume(buffer.size());
  websocket.read(buffer);
  buffer.consume(buffer.size());

  auto stop_response = SendRequest(kRemotePort, http::verb::post, "/sessions/s_1/stop", "", token);
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
  const std::string create_response = CreateSession(token);
  ASSERT_NE(create_response.find("\"sessionId\":\"s_1\""), std::string::npos);

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
  websocket.handshake("127.0.0.1:" + std::to_string(kRemotePort), "/ws/sessions/s_1");

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
  ASSERT_NE(create_response.find("\"sessionId\":\"s_1\""), std::string::npos);

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
  websocket.handshake("127.0.0.1:" + std::to_string(kRemotePort), "/ws/sessions/s_1");

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
  ASSERT_NE(create_response.find("\"sessionId\":\"s_1\""), std::string::npos);

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
  websocket.handshake("127.0.0.1:" + std::to_string(kRemotePort), "/ws/sessions/s_1");

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
  ASSERT_NE(create_response.find("\"sessionId\":\"s_1\""), std::string::npos);

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
  websocket.handshake("127.0.0.1:" + std::to_string(kRemotePort), "/ws/sessions/s_1");

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
  ASSERT_NE(create_response.find("\"sessionId\":\"s_1\""), std::string::npos);

  asio::io_context io_context;
  tcp::resolver resolver(io_context);
  websocket::stream<tcp::socket> websocket(io_context);
  const auto results = resolver.resolve("127.0.0.1", std::to_string(kRemotePort));
  auto endpoint = asio::connect(websocket.next_layer(), results);
  static_cast<void>(endpoint);

  boost::system::system_error error(http::error::end_of_stream);
  try {
    websocket.handshake("127.0.0.1:" + std::to_string(kRemotePort), "/ws/sessions/s_1");
  } catch (const boost::system::system_error& exception) {
    error = exception;
  }

  EXPECT_EQ(error.code(), websocket::error::upgrade_declined);
}

TEST_F(HttpServerFixture, WebSocketSessionEndpointAcceptsAccessTokenQueryParameter) {
  const std::string token = EnsureApprovedToken();
  const std::string create_response = CreateSession(token);
  ASSERT_NE(create_response.find("\"sessionId\":\"s_1\""), std::string::npos);

  asio::io_context io_context;
  tcp::resolver resolver(io_context);
  websocket::stream<tcp::socket> websocket(io_context);
  const auto results = resolver.resolve("127.0.0.1", std::to_string(kRemotePort));
  auto endpoint = asio::connect(websocket.next_layer(), results);
  static_cast<void>(endpoint);

  websocket.handshake("127.0.0.1:" + std::to_string(kRemotePort), "/ws/sessions/s_1?access_token=" + token);

  beast::flat_buffer buffer;
  websocket.read(buffer);
  const std::string payload = beast::buffers_to_string(buffer.data());
  EXPECT_NE(payload.find("\"type\":\"session.updated\""), std::string::npos);
  EXPECT_NE(payload.find("\"sessionId\":\"s_1\""), std::string::npos);
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

TEST_F(HttpServerFixture, RemoteListenerUsesPlainHttpWhenTlsDisabled) {
  auto response = SendRequest(kRemotePort, http::verb::get, "/host/info");
  EXPECT_EQ(response.result(), http::status::ok);
  EXPECT_NE(response.body().find("\"tls\":{\"enabled\":false"), std::string::npos);
}

TEST_F(HttpServerFixture, ServerFailsToStartWhenPersistedRemoteTlsConfigIsIncomplete) {
  StopServer();
  WriteHostIdentity("/tmp/remote-cert.pem", "");

  testing::internal::CaptureStderr();
  HttpServer server("127.0.0.1", kAdminPort, "127.0.0.1", kRemotePort, storage_root_);
  EXPECT_FALSE(server.Run());
  const std::string stderr_output = testing::internal::GetCapturedStderr();
  EXPECT_NE(stderr_output.find("both certificatePemPath and privateKeyPemPath are required"),
            std::string::npos);
}

TEST_F(HttpServerFixture, ServerFailsToStartWhenPersistedRemoteTlsFilesAreMissing) {
  StopServer();
  WriteHostIdentity((storage_root_ / "missing-cert.pem").string(),
                    (storage_root_ / "missing-key.pem").string());

  testing::internal::CaptureStderr();
  HttpServer server("127.0.0.1", kAdminPort, "127.0.0.1", kRemotePort, storage_root_);
  EXPECT_FALSE(server.Run());
  const std::string stderr_output = testing::internal::GetCapturedStderr();
  EXPECT_NE(stderr_output.find("remote TLS certificate file not found"), std::string::npos);
}

TEST_F(HttpServerFixture, RemoteListenerSupportsHttpsAndWssWhenTlsConfigured) {
  StopServer();
  EnablePersistedRemoteTls();
  StartServer();

  auto secure_host_info = SendSecureRequest(kRemotePort, http::verb::get, "/host/info");
  EXPECT_EQ(secure_host_info.result(), http::status::ok);
  EXPECT_NE(secure_host_info.body().find("\"tls\":{\"enabled\":true"), std::string::npos);

  auto admin_health = SendRequest(kAdminPort, http::verb::get, "/health");
  EXPECT_EQ(admin_health.result(), http::status::ok);

  const std::string token = EnsureApprovedSecureToken();
  const std::string create_response = CreateSecureSession(token);
  ASSERT_NE(create_response.find("\"sessionId\":\"s_1\""), std::string::npos);

  asio::io_context io_context;
  ssl::context ssl_context(ssl::context::tls_client);
  ssl_context.set_verify_mode(ssl::verify_none);
  tcp::resolver resolver(io_context);
  websocket::stream<ssl::stream<tcp::socket>> websocket(io_context, ssl_context);
  const auto results = resolver.resolve("127.0.0.1", std::to_string(kRemotePort));
  auto endpoint = asio::connect(websocket.next_layer().next_layer(), results);
  static_cast<void>(endpoint);

  websocket.next_layer().handshake(ssl::stream_base::client);
  websocket.set_option(websocket::stream_base::decorator(
      [&token](websocket::request_type& request) {
        request.set(http::field::authorization, "Bearer " + token);
      }));
  websocket.handshake("127.0.0.1:" + std::to_string(kRemotePort), "/ws/sessions/s_1");

  beast::flat_buffer buffer;
  websocket.read(buffer);
  const std::string payload = beast::buffers_to_string(buffer.data());
  EXPECT_NE(payload.find("\"type\":\"session.updated\""), std::string::npos);
  EXPECT_NE(payload.find("\"sessionId\":\"s_1\""), std::string::npos);
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
