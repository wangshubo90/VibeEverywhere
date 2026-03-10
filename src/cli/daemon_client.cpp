#include "vibe/cli/daemon_client.h"

#include <sys/ioctl.h>
#include <sys/select.h>
#include <unistd.h>

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/json.hpp>

#include <chrono>
#include <iostream>
#include <thread>

namespace vibe::cli {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace json = boost::json;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;

namespace {

auto BuildWebSocketTarget(const std::string& session_id) -> std::string {
  return "/ws/sessions/" + session_id;
}

auto ToStringPort(const std::uint16_t port) -> std::string { return std::to_string(port); }

auto DetectTerminalSize() -> vibe::session::TerminalSize {
  winsize size{};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0 && size.ws_row > 0) {
    return vibe::session::TerminalSize{
        .columns = size.ws_col,
        .rows = size.ws_row,
    };
  }

  return vibe::session::TerminalSize{};
}

auto HandleServerMessage(const std::string& payload, int& exit_code) -> bool {
  boost::system::error_code error_code;
  const json::value parsed = json::parse(payload, error_code);
  if (error_code || !parsed.is_object()) {
    return true;
  }

  const json::object& object = parsed.as_object();
  const auto type = object.if_contains("type");
  if (type == nullptr || !type->is_string()) {
    return true;
  }

  const std::string message_type = json::value_to<std::string>(*type);
  if (message_type == "terminal.output") {
    if (const auto data = object.if_contains("data"); data != nullptr && data->is_string()) {
      std::cout << json::value_to<std::string>(*data) << std::flush;
    }
    return true;
  }

  if (message_type == "session.exited") {
    exit_code = 0;
    if (const auto status = object.if_contains("status"); status != nullptr && status->is_string() &&
        json::value_to<std::string>(*status) != "Exited") {
      exit_code = 1;
    }
    return false;
  }

  if (message_type == "error") {
    const auto message = object.if_contains("message");
    if (message != nullptr && message->is_string()) {
      std::cerr << "[error] " << json::value_to<std::string>(*message) << '\n';
    }
    return true;
  }

  return true;
}

}  // namespace

auto BuildCreateSessionRequestBody(const vibe::session::ProviderType provider,
                                   const std::string& workspace_root,
                                   const std::string& title) -> std::string {
  json::object object;
  object["provider"] = std::string(vibe::session::ToString(provider));
  object["workspaceRoot"] = workspace_root;
  object["title"] = title;
  return json::serialize(object);
}

auto ParseCreatedSessionId(const std::string& body) -> std::optional<std::string> {
  boost::system::error_code error_code;
  const json::value parsed = json::parse(body, error_code);
  if (error_code || !parsed.is_object()) {
    return std::nullopt;
  }

  const json::object& object = parsed.as_object();
  const auto session_id = object.if_contains("sessionId");
  if (session_id == nullptr || !session_id->is_string()) {
    return std::nullopt;
  }

  return json::value_to<std::string>(*session_id);
}

auto BuildControlRequestCommand(const vibe::session::ControllerKind controller_kind) -> std::string {
  json::object object;
  object["type"] = "session.control.request";
  object["kind"] = std::string(vibe::session::ToString(controller_kind));
  return json::serialize(object);
}

auto BuildReleaseControlCommand() -> std::string {
  json::object object;
  object["type"] = "session.control.release";
  return json::serialize(object);
}

auto BuildInputCommand(const std::string& data) -> std::string {
  json::object object;
  object["type"] = "terminal.input";
  object["data"] = data;
  return json::serialize(object);
}

auto BuildResizeCommand(const vibe::session::TerminalSize terminal_size) -> std::string {
  json::object object;
  object["type"] = "terminal.resize";
  object["cols"] = terminal_size.columns;
  object["rows"] = terminal_size.rows;
  return json::serialize(object);
}

auto CreateSession(const DaemonEndpoint& endpoint, const vibe::session::ProviderType provider,
                   const std::string& workspace_root, const std::string& title)
    -> std::optional<std::string> {
  asio::io_context io_context;
  tcp::resolver resolver(io_context);
  tcp::socket socket(io_context);
  boost::system::error_code error_code;
  const auto results = resolver.resolve(endpoint.host, ToStringPort(endpoint.port), error_code);
  if (error_code) {
    std::cerr << "resolve failed: " << error_code.message() << '\n';
    return std::nullopt;
  }

  const auto endpoint_result = asio::connect(socket, results, error_code);
  static_cast<void>(endpoint_result);
  if (error_code) {
    std::cerr << "connect failed: " << error_code.message() << '\n';
    return std::nullopt;
  }

  http::request<http::string_body> request{http::verb::post, "/sessions", 11};
  request.set(http::field::host, endpoint.host);
  request.set(http::field::content_type, "application/json");
  request.body() = BuildCreateSessionRequestBody(provider, workspace_root, title);
  request.prepare_payload();

  http::write(socket, request, error_code);
  if (error_code) {
    std::cerr << "http write failed: " << error_code.message() << '\n';
    return std::nullopt;
  }

  beast::flat_buffer buffer;
  http::response<http::string_body> response;
  http::read(socket, buffer, response, error_code);
  if (error_code) {
    std::cerr << "http read failed: " << error_code.message() << '\n';
    return std::nullopt;
  }

  if (response.result() != http::status::created) {
    return std::nullopt;
  }

  return ParseCreatedSessionId(response.body());
}

auto AttachSession(const DaemonEndpoint& endpoint, const std::string& session_id,
                   const vibe::session::ControllerKind controller_kind) -> int {
  asio::io_context io_context;
  tcp::resolver resolver(io_context);
  websocket::stream<tcp::socket> ws(io_context);
  boost::system::error_code error_code;
  const auto results = resolver.resolve(endpoint.host, ToStringPort(endpoint.port), error_code);
  if (error_code) {
    std::cerr << "resolve failed: " << error_code.message() << '\n';
    return 1;
  }

  const auto endpoint_result = asio::connect(ws.next_layer(), results, error_code);
  static_cast<void>(endpoint_result);
  if (error_code) {
    std::cerr << "connect failed: " << error_code.message() << '\n';
    return 1;
  }

  ws.handshake(endpoint.host + ":" + ToStringPort(endpoint.port), BuildWebSocketTarget(session_id),
               error_code);
  if (error_code) {
    std::cerr << "websocket handshake failed: " << error_code.message() << '\n';
    return 1;
  }

  auto write_command = [&ws](const std::string& command) -> bool {
    boost::system::error_code write_error;
    ws.write(asio::buffer(command), write_error);
    return !write_error;
  };

  if (!write_command(BuildControlRequestCommand(controller_kind))) {
    std::cerr << "failed to request control\n";
    return 1;
  }
  if (!write_command(BuildResizeCommand(DetectTerminalSize()))) {
    std::cerr << "failed to send initial resize\n";
    return 1;
  }

  beast::flat_buffer buffer;
  int exit_code = 0;
  bool stdin_open = true;
  const int websocket_fd = ws.next_layer().native_handle();

  while (true) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(websocket_fd, &read_fds);

    int max_fd = websocket_fd;
    if (stdin_open) {
      FD_SET(STDIN_FILENO, &read_fds);
      if (STDIN_FILENO > max_fd) {
        max_fd = STDIN_FILENO;
      }
    }

    timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = 20000;

    const int select_result = select(max_fd + 1, &read_fds, nullptr, nullptr, &timeout);
    if (select_result < 0) {
      continue;
    }

    if (FD_ISSET(websocket_fd, &read_fds)) {
      error_code.clear();
      ws.read(buffer, error_code);
      if (!error_code) {
        const std::string payload = beast::buffers_to_string(buffer.data());
        buffer.consume(buffer.size());
        if (!HandleServerMessage(payload, exit_code)) {
          return exit_code;
        }
      } else if (error_code == websocket::error::closed) {
        return exit_code;
      } else {
        std::cerr << "websocket read failed: " << error_code.message() << '\n';
        return 1;
      }
    }

    if (stdin_open && FD_ISSET(STDIN_FILENO, &read_fds)) {
        char input_buffer[1024];
        const ssize_t bytes_read = read(STDIN_FILENO, input_buffer, sizeof(input_buffer));
        if (bytes_read == 0) {
          const bool released = write_command(BuildReleaseControlCommand());
          static_cast<void>(released);
          return 0;
        } else if (bytes_read > 0) {
          const bool wrote = write_command(
              BuildInputCommand(std::string(input_buffer, static_cast<std::size_t>(bytes_read))));
          if (!wrote) {
            std::cerr << "failed to send terminal input\n";
            return 1;
          }
        }
    }
  }
}

}  // namespace vibe::cli
