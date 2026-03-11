#include "vibe/cli/daemon_client.h"

#include <sys/ioctl.h>
#include <sys/select.h>
#include <unistd.h>

#include <signal.h>
#include <termios.h>

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/json.hpp>

#include <array>
#include <chrono>
#include <iostream>
#include <vector>
#include <thread>

namespace vibe::cli {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace json = boost::json;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;

namespace {

volatile sig_atomic_t g_terminal_resize_pending = 0;

void HandleWindowResizeSignal(int /*signal_number*/) { g_terminal_resize_pending = 1; }

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

[[nodiscard]] auto DecodeBase64(const std::string_view input) -> std::optional<std::string> {
  std::array<int, 256> decoding_table{};
  decoding_table.fill(-1);
  for (int index = 0; index < 26; ++index) {
    decoding_table[static_cast<unsigned char>('A' + index)] = index;
    decoding_table[static_cast<unsigned char>('a' + index)] = 26 + index;
  }
  for (int index = 0; index < 10; ++index) {
    decoding_table[static_cast<unsigned char>('0' + index)] = 52 + index;
  }
  decoding_table[static_cast<unsigned char>('+')] = 62;
  decoding_table[static_cast<unsigned char>('/')] = 63;

  if (input.size() % 4U != 0U) {
    return std::nullopt;
  }

  std::string decoded;
  decoded.reserve((input.size() / 4U) * 3U);
  for (std::size_t index = 0; index < input.size(); index += 4U) {
    const char c0 = input[index];
    const char c1 = input[index + 1U];
    const char c2 = input[index + 2U];
    const char c3 = input[index + 3U];

    if (decoding_table[static_cast<unsigned char>(c0)] < 0 ||
        decoding_table[static_cast<unsigned char>(c1)] < 0 ||
        (c2 != '=' && decoding_table[static_cast<unsigned char>(c2)] < 0) ||
        (c3 != '=' && decoding_table[static_cast<unsigned char>(c3)] < 0)) {
      return std::nullopt;
    }

    const int v0 = decoding_table[static_cast<unsigned char>(c0)];
    const int v1 = decoding_table[static_cast<unsigned char>(c1)];
    const int v2 = c2 == '=' ? 0 : decoding_table[static_cast<unsigned char>(c2)];
    const int v3 = c3 == '=' ? 0 : decoding_table[static_cast<unsigned char>(c3)];

    decoded.push_back(static_cast<char>((v0 << 2) | (v1 >> 4)));
    if (c2 != '=') {
      decoded.push_back(static_cast<char>(((v1 & 0x0F) << 4) | (v2 >> 2)));
    }
    if (c3 != '=') {
      decoded.push_back(static_cast<char>(((v2 & 0x03) << 6) | v3));
    }
  }

  return decoded;
}

class ScopedSignalHandler {
 public:
  ScopedSignalHandler(const int signal_number, void (*handler)(int))
      : signal_number_(signal_number), previous_handler_(std::signal(signal_number, handler)) {}

  ScopedSignalHandler(const ScopedSignalHandler&) = delete;
  auto operator=(const ScopedSignalHandler&) -> ScopedSignalHandler& = delete;

  ~ScopedSignalHandler() { std::signal(signal_number_, previous_handler_); }

 private:
  int signal_number_{0};
  using SignalHandler = void (*)(int);
  SignalHandler previous_handler_{SIG_DFL};
};

class ScopedRawTerminalMode {
 public:
  ScopedRawTerminalMode() {
    if (!isatty(STDIN_FILENO)) {
      return;
    }

    if (tcgetattr(STDIN_FILENO, &original_attributes_) != 0) {
      return;
    }

    termios raw_attributes = original_attributes_;
    raw_attributes.c_iflag &= static_cast<tcflag_t>(~(BRKINT | ICRNL | INPCK | ISTRIP | IXON));
    raw_attributes.c_oflag &= static_cast<tcflag_t>(~OPOST);
    raw_attributes.c_cflag |= CS8;
    raw_attributes.c_lflag &=
        static_cast<tcflag_t>(~(ECHO | ICANON | IEXTEN | ISIG));
    raw_attributes.c_cc[VMIN] = 0;
    raw_attributes.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_attributes) == 0) {
      active_ = true;
    }
  }

  ScopedRawTerminalMode(const ScopedRawTerminalMode&) = delete;
  auto operator=(const ScopedRawTerminalMode&) -> ScopedRawTerminalMode& = delete;

  ~ScopedRawTerminalMode() {
    if (!active_) {
      return;
    }

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_attributes_);
  }

  [[nodiscard]] auto active() const -> bool { return active_; }

 private:
  termios original_attributes_{};
  bool active_{false};
};

struct SessionStreamState {
  bool has_control{false};
  bool control_request_pending{false};
  std::string active_controller_kind{"none"};
  bool active_controller_has_client{false};
};

auto HandleServerMessage(const std::string& payload,
                         const vibe::session::ControllerKind controller_kind,
                         SessionStreamState& stream_state,
                         int& exit_code) -> bool {
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
    if (const auto encoded = object.if_contains("dataBase64");
        encoded != nullptr && encoded->is_string()) {
      const auto decoded = DecodeBase64(json::value_to<std::string>(*encoded));
      if (decoded.has_value()) {
        std::cout.write(decoded->data(), static_cast<std::streamsize>(decoded->size()));
        std::cout.flush();
      }
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

  if (message_type == "session.updated") {
    const auto controller_kind_value = object.if_contains("controllerKind");
    const auto controller_client_id = object.if_contains("controllerClientId");
    if (controller_kind_value != nullptr && controller_kind_value->is_string()) {
      const std::string active_controller_kind =
          json::value_to<std::string>(*controller_kind_value);

      if (controller_kind == vibe::session::ControllerKind::Host) {
        stream_state.active_controller_kind = active_controller_kind;
        stream_state.active_controller_has_client =
            controller_client_id != nullptr && controller_client_id->is_string();
        if (active_controller_kind == "remote") {
          stream_state.has_control = false;
          stream_state.control_request_pending = false;
        } else if (active_controller_kind == "host") {
          if (stream_state.control_request_pending ||
              (controller_client_id != nullptr && controller_client_id->is_string())) {
            stream_state.has_control = true;
            stream_state.control_request_pending = false;
          } else {
            stream_state.has_control = false;
          }
        } else {
          stream_state.has_control = false;
          stream_state.control_request_pending = false;
        }
      } else if (controller_kind == vibe::session::ControllerKind::Remote) {
        stream_state.active_controller_kind = active_controller_kind;
        stream_state.active_controller_has_client =
            controller_client_id != nullptr && controller_client_id->is_string();
        stream_state.has_control =
            active_controller_kind == "remote" &&
            controller_client_id != nullptr && controller_client_id->is_string();
        if (stream_state.has_control) {
          stream_state.control_request_pending = false;
        }
      }
    }
    return true;
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

auto ParseSessionList(const std::string& body) -> std::vector<ListedSession> {
  boost::system::error_code error_code;
  const json::value parsed = json::parse(body, error_code);
  if (error_code || !parsed.is_array()) {
    return {};
  }

  std::vector<ListedSession> sessions;
  for (const auto& value : parsed.as_array()) {
    if (!value.is_object()) {
      continue;
    }
    const auto& object = value.as_object();
    const auto session_id = object.if_contains("sessionId");
    if (session_id == nullptr || !session_id->is_string()) {
      continue;
    }

    ListedSession session;
    session.session_id = json::value_to<std::string>(*session_id);
    if (const auto title = object.if_contains("title"); title != nullptr && title->is_string()) {
      session.title = json::value_to<std::string>(*title);
    }
    if (const auto activity_state = object.if_contains("activityState");
        activity_state != nullptr && activity_state->is_string()) {
      session.activity_state = json::value_to<std::string>(*activity_state);
    }
    if (const auto status = object.if_contains("status"); status != nullptr && status->is_string()) {
      session.status = json::value_to<std::string>(*status);
    }
    sessions.push_back(std::move(session));
  }

  return sessions;
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

auto ListSessions(const DaemonEndpoint& endpoint) -> std::optional<std::vector<ListedSession>> {
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

  http::request<http::string_body> request{http::verb::get, "/sessions", 11};
  request.set(http::field::host, endpoint.host);
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

  if (response.result() != http::status::ok) {
    return std::nullopt;
  }

  return ParseSessionList(response.body());
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
  SessionStreamState stream_state{
      .has_control = false,
      .control_request_pending = true,
      .active_controller_kind = "none",
      .active_controller_has_client = false,
  };
  ScopedRawTerminalMode raw_terminal_mode;
  ScopedSignalHandler window_resize_handler(SIGWINCH, HandleWindowResizeSignal);
  auto last_terminal_size = DetectTerminalSize();
  bool resize_sync_pending = true;
  beast::flat_buffer buffer;
  int exit_code = 0;
  bool stdin_open = true;
  const int websocket_fd = ws.next_layer().native_handle();

  while (true) {
    if (controller_kind == vibe::session::ControllerKind::Host &&
        !stream_state.has_control &&
        !stream_state.control_request_pending &&
        stream_state.active_controller_kind == "host" &&
        !stream_state.active_controller_has_client) {
      const bool requested = write_command(BuildControlRequestCommand(controller_kind));
      if (!requested) {
        std::cerr << "failed to reclaim host control\n";
        return 1;
      }
      stream_state.control_request_pending = true;
    }

    if (g_terminal_resize_pending != 0) {
      g_terminal_resize_pending = 0;
      const auto current_terminal_size = DetectTerminalSize();
      if (current_terminal_size != last_terminal_size) {
        last_terminal_size = current_terminal_size;
        resize_sync_pending = true;
      }
    }

    if (stream_state.has_control && resize_sync_pending) {
      const bool wrote_resize = write_command(BuildResizeCommand(last_terminal_size));
      if (!wrote_resize) {
        std::cerr << "failed to send resize update\n";
        return 1;
      }
      resize_sync_pending = false;
    }

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
        if (!HandleServerMessage(payload, controller_kind, stream_state, exit_code)) {
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
          if (!stream_state.has_control) {
            continue;
          }
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
