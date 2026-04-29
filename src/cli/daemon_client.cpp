#include "vibe/cli/daemon_client.h"

#include <sys/ioctl.h>
#include <unistd.h>

#include <signal.h>
#include <termios.h>

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/json.hpp>

#include <array>
#include <chrono>
#include <iostream>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <vector>
#include <thread>

#include "vibe/net/local_auth.h"

namespace vibe::cli {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace json = boost::json;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;
using local_stream = asio::local::stream_protocol;

namespace {

volatile sig_atomic_t g_terminal_resize_pending = 0;

void HandleWindowResizeSignal(int /*signal_number*/) { g_terminal_resize_pending = 1; }

class AttachTraceLogger {
 public:
  AttachTraceLogger() {
    const char* path = std::getenv("VIBE_ATTACH_TRACE_PATH");
    if (path == nullptr || *path == '\0') {
      return;
    }

    output_ = std::make_unique<std::ofstream>(path, std::ios::out | std::ios::trunc);
    if (output_ == nullptr || !output_->is_open()) {
      output_.reset();
      return;
    }
    start_time_ = std::chrono::steady_clock::now();
  }

  void Log(const std::string_view event, const std::size_t value = 0) {
    if (output_ == nullptr) {
      return;
    }

    const auto elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time_)
            .count();
    std::lock_guard<std::mutex> lock(mutex_);
    (*output_) << elapsed << ' ' << event << ' ' << value << '\n';
    output_->flush();
  }

 private:
  std::unique_ptr<std::ofstream> output_;
  std::chrono::steady_clock::time_point start_time_{};
  std::mutex mutex_;
};

auto BuildWebSocketTarget(const std::string& session_id) -> std::string {
  return "/ws/sessions/" + session_id + "?stream=raw";
}

struct ParsedWebSocketBaseUrl {
  std::string scheme;
  std::string host;
  std::string port;
  std::string path_prefix;
};

auto ParseWebSocketBaseUrl(const std::string& url) -> std::optional<ParsedWebSocketBaseUrl> {
  ParsedWebSocketBaseUrl parsed;
  std::string remainder;

  if (url.starts_with("http://")) {
    parsed.scheme = "ws";
    parsed.port = "80";
    remainder = url.substr(7);
  } else if (url.starts_with("ws://")) {
    parsed.scheme = "ws";
    parsed.port = "80";
    remainder = url.substr(5);
  } else if (url.starts_with("https://")) {
    parsed.scheme = "wss";
    parsed.port = "443";
    remainder = url.substr(8);
  } else if (url.starts_with("wss://")) {
    parsed.scheme = "wss";
    parsed.port = "443";
    remainder = url.substr(6);
  } else {
    return std::nullopt;
  }

  const auto slash_pos = remainder.find('/');
  const std::string authority =
      slash_pos == std::string::npos ? remainder : remainder.substr(0, slash_pos);
  parsed.path_prefix = slash_pos == std::string::npos ? "" : remainder.substr(slash_pos);

  const auto colon_pos = authority.find(':');
  if (colon_pos != std::string::npos) {
    parsed.host = authority.substr(0, colon_pos);
    parsed.port = authority.substr(colon_pos + 1);
  } else {
    parsed.host = authority;
  }
  if (parsed.host.empty()) {
    return std::nullopt;
  }
  if (!parsed.path_prefix.empty() && parsed.path_prefix.back() == '/') {
    parsed.path_prefix.pop_back();
  }
  return parsed;
}

auto IsLocalEndpointHost(const std::string_view host) -> bool {
  return host == "127.0.0.1" || host == "localhost" || host == "::1";
}

auto BuildLocalControllerHandshake(const std::string& session_id,
                                   const vibe::session::TerminalSize terminal_size)
    -> std::string {
  json::object object;
  object["sessionId"] = session_id;
  object["columns"] = terminal_size.columns;
  object["rows"] = terminal_size.rows;
  return json::serialize(object) + "\n";
}

auto BuildLocalControllerFrame(const char type, const std::string_view payload) -> std::string {
  std::string frame;
  frame.resize(5U + payload.size());
  frame[0] = type;
  const std::uint32_t size = static_cast<std::uint32_t>(payload.size());
  frame[1] = static_cast<char>((size >> 24) & 0xFF);
  frame[2] = static_cast<char>((size >> 16) & 0xFF);
  frame[3] = static_cast<char>((size >> 8) & 0xFF);
  frame[4] = static_cast<char>(size & 0xFF);
  if (!payload.empty()) {
    std::memcpy(frame.data() + 5, payload.data(), payload.size());
  }
  return frame;
}

auto BuildLocalResizePayload(const vibe::session::TerminalSize terminal_size) -> std::string {
  std::string payload(4, '\0');
  payload[0] = static_cast<char>((terminal_size.columns >> 8) & 0xFF);
  payload[1] = static_cast<char>(terminal_size.columns & 0xFF);
  payload[2] = static_cast<char>((terminal_size.rows >> 8) & 0xFF);
  payload[3] = static_cast<char>(terminal_size.rows & 0xFF);
  return payload;
}

struct FilteredInput {
  bool saw_reclaim{false};
  bool saw_detach{false};
  std::string payload;
};

auto FilterLocalReclaimInput(const std::string_view input) -> FilteredInput {
  constexpr std::array<std::string_view, 3> kReclaimSequences = {
      std::string_view("\x1d", 1),          // classic Ctrl-]
      std::string_view("\x1b[93;5u", 8),    // CSI-u Ctrl-]
      std::string_view("\x1b[27;5;93~", 10) // xterm modifyOtherKeys Ctrl-]
  };
  constexpr std::array<std::string_view, 3> kDetachSequences = {
      std::string_view("\x1c", 1),          // classic Ctrl+backslash
      std::string_view("\x1b[92;5u", 8),    // CSI-u Ctrl+backslash
      std::string_view("\x1b[27;5;92~", 10) // xterm modifyOtherKeys Ctrl+backslash
  };

  FilteredInput result{
      .saw_reclaim = false,
      .saw_detach = false,
      .payload = std::string(input),
  };

  for (const std::string_view sequence : kReclaimSequences) {
    std::size_t position = 0;
    while ((position = result.payload.find(sequence, position)) != std::string::npos) {
      result.saw_reclaim = true;
      result.payload.erase(position, sequence.size());
    }
  }

  for (const std::string_view sequence : kDetachSequences) {
    std::size_t position = 0;
    while ((position = result.payload.find(sequence, position)) != std::string::npos) {
      result.saw_detach = true;
      result.payload.erase(position, sequence.size());
    }
  }

  if (!result.saw_reclaim && !result.saw_detach) {
    return result;
  }

  // Kitty/ghostty-style enhanced keyboard sequences may include an event-type suffix.
  for (const auto [kitty_prefix, detach] : {
           std::pair<std::string_view, bool>{"\x1b[93;5:", false},
           std::pair<std::string_view, bool>{"\x1b[92;5:", true},
       }) {
    std::size_t position = 0;
    while ((position = result.payload.find(kitty_prefix, position)) != std::string::npos) {
      const std::size_t digits_begin = position + kitty_prefix.size();
      std::size_t digits_end = digits_begin;
      while (digits_end < result.payload.size() &&
             result.payload[digits_end] >= '0' && result.payload[digits_end] <= '9') {
        digits_end += 1;
      }
      if (digits_end > digits_begin && digits_end < result.payload.size() &&
          result.payload[digits_end] == 'u') {
        if (detach) {
          result.saw_detach = true;
        } else {
          result.saw_reclaim = true;
        }
        result.payload.erase(position, digits_end - position + 1);
        continue;
      }
      position = digits_begin;
    }
  }

  return result;
}

auto ToStringPort(const std::uint16_t port) -> std::string { return std::to_string(port); }

void WriteAllToStdout(const std::string_view data) {
  const char* bytes = data.data();
  std::size_t remaining = data.size();
  while (remaining > 0U) {
    const ssize_t written = write(STDOUT_FILENO, bytes, remaining);
    if (written <= 0) {
      break;
    }
    bytes += written;
    remaining -= static_cast<std::size_t>(written);
  }
}

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
    raw_attributes.c_cc[VMIN] = 1;
    raw_attributes.c_cc[VTIME] = 0;

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

void RestoreTerminalEscapeModes() {
  if (!isatty(STDOUT_FILENO)) {
    return;
  }

  // Reset common interactive terminal modes that a remote PTY/app may have enabled:
  // application cursor keys, alternate screen, bracketed paste, focus/mouse reporting,
  // xterm modifyOtherKeys, and kitty keyboard protocol.
  constexpr std::string_view kResetSequence =
      "\x1b[?1l"
      "\x1b[?1049l"
      "\x1b[?2004l"
      "\x1b[?1004l"
      "\x1b[?1002l"
      "\x1b[?1003l"
      "\x1b[?1006l"
      "\x1b[>4;0m"
      "\x1b[<u";

  const auto* data = kResetSequence.data();
  std::size_t remaining = kResetSequence.size();
  while (remaining > 0U) {
    const ssize_t written = write(STDOUT_FILENO, data, remaining);
    if (written <= 0) {
      break;
    }
    data += written;
    remaining -= static_cast<std::size_t>(written);
  }
}

class ScopedTerminalEscapeReset {
 public:
  ScopedTerminalEscapeReset() = default;

  ScopedTerminalEscapeReset(const ScopedTerminalEscapeReset&) = delete;
  auto operator=(const ScopedTerminalEscapeReset&) -> ScopedTerminalEscapeReset& = delete;

  ~ScopedTerminalEscapeReset() { RestoreTerminalEscapeModes(); }
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
        WriteAllToStdout(*decoded);
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

auto AttachSessionLocal(const std::string& session_id) -> int {
  AttachTraceLogger trace_logger;
  asio::io_context io_context;
  local_stream::socket socket(io_context);
  boost::system::error_code error_code;
  const auto socket_path = vibe::net::DefaultControllerSocketPath(vibe::net::DefaultStorageRoot());
  socket.connect(local_stream::endpoint(socket_path.string()), error_code);
  if (error_code) {
    return -1;
  }

  const auto initial_terminal_size = DetectTerminalSize();
  const std::string handshake =
      BuildLocalControllerHandshake(session_id, initial_terminal_size);
  asio::write(socket, asio::buffer(handshake), error_code);
  if (error_code) {
    std::cerr << "local controller handshake write failed: " << error_code.message() << '\n';
    return 1;
  }

  std::string handshake_response;
  std::array<char, 256> handshake_chunk{};
  while (handshake_response.find('\n') == std::string::npos) {
    const std::size_t bytes_read = socket.read_some(asio::buffer(handshake_chunk), error_code);
    if (error_code) {
      std::cerr << "local controller handshake failed: " << error_code.message() << '\n';
      return 1;
    }
    handshake_response.append(handshake_chunk.data(), bytes_read);
    if (handshake_response.size() > 4096U) {
      std::cerr << "local controller handshake failed: oversized response\n";
      return 1;
    }
  }
  handshake_response.resize(handshake_response.find('\n'));
  if (handshake_response != "OK") {
    std::cerr << (handshake_response.empty() ? "local controller attach rejected"
                                             : handshake_response)
              << '\n';
    return 1;
  }

  std::atomic<bool> stop_requested{false};
  std::atomic<bool> read_failed{false};
  std::string read_error_message;

  ScopedTerminalEscapeReset terminal_escape_reset;
  ScopedRawTerminalMode raw_terminal_mode;
  ScopedSignalHandler window_resize_handler(SIGWINCH, HandleWindowResizeSignal);
  auto last_terminal_size = initial_terminal_size;
  bool resize_sync_pending = false;
  bool stdin_open = true;

  std::thread reader_thread([&]() {
    std::array<char, 4096> buffer{};
    while (!stop_requested.load()) {
      boost::system::error_code read_error;
      const std::size_t bytes_read = socket.read_some(asio::buffer(buffer), read_error);
      if (read_error == asio::error::eof) {
        stop_requested.store(true);
        return;
      }
      if (read_error) {
        read_error_message = read_error.message();
        read_failed.store(true);
        stop_requested.store(true);
        return;
      }
      if (bytes_read == 0) {
        continue;
      }

      trace_logger.Log("ws.read.binary", bytes_read);
      trace_logger.Log("stdout.write", bytes_read);
      WriteAllToStdout(std::string_view(buffer.data(), bytes_read));
    }
  });

  while (!stop_requested.load()) {
    if (g_terminal_resize_pending != 0) {
      g_terminal_resize_pending = 0;
      const auto current_terminal_size = DetectTerminalSize();
      if (current_terminal_size != last_terminal_size) {
        last_terminal_size = current_terminal_size;
        resize_sync_pending = true;
      }
    }

    if (resize_sync_pending) {
      const auto payload = BuildLocalResizePayload(last_terminal_size);
      const auto frame = BuildLocalControllerFrame('R', payload);
      asio::write(socket, asio::buffer(frame), error_code);
      if (error_code) {
        stop_requested.store(true);
        break;
      }
        trace_logger.Log("ws.write.resize");
      resize_sync_pending = false;
    }

    fd_set read_fds;
    FD_ZERO(&read_fds);
    int max_fd = -1;
    if (stdin_open) {
      FD_SET(STDIN_FILENO, &read_fds);
      max_fd = STDIN_FILENO;
    }

    timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = 20000;
    const int select_result =
        max_fd >= 0 ? select(max_fd + 1, &read_fds, nullptr, nullptr, &timeout) : 0;
    if (select_result < 0) {
      continue;
    }

    if (stdin_open && FD_ISSET(STDIN_FILENO, &read_fds)) {
      char input_buffer[1024];
      const ssize_t bytes_read = read(STDIN_FILENO, input_buffer, sizeof(input_buffer));
      if (bytes_read == 0) {
        stdin_open = false;
        stop_requested.store(true);
        break;
      }
      if (bytes_read < 0) {
        continue;
      }

      trace_logger.Log("stdin.read", static_cast<std::size_t>(bytes_read));
      const auto filtered =
          FilterLocalReclaimInput(std::string_view(input_buffer, static_cast<std::size_t>(bytes_read)));

      if (filtered.saw_reclaim) {
        const auto reclaim_frame = BuildLocalControllerFrame('C', {});
        asio::write(socket, asio::buffer(reclaim_frame), error_code);
        if (error_code) {
          stop_requested.store(true);
          break;
        }
        trace_logger.Log("ws.write.reclaim", static_cast<std::size_t>(bytes_read));
      }

      if (filtered.saw_detach) {
        trace_logger.Log("ws.write.detach", static_cast<std::size_t>(bytes_read));
        stop_requested.store(true);
      }

      if (!filtered.payload.empty()) {
        const auto input_frame = BuildLocalControllerFrame('I', filtered.payload);
        asio::write(socket, asio::buffer(input_frame), error_code);
        if (error_code) {
          stop_requested.store(true);
          break;
        }
        trace_logger.Log("ws.write.input", filtered.payload.size());
      }

      if (filtered.saw_detach) {
        break;
      }
    }
  }

  boost::system::error_code close_error;
  socket.close(close_error);
  if (reader_thread.joinable()) {
    reader_thread.join();
  }
  if (read_failed.load()) {
    std::cerr << "local controller read failed: " << read_error_message << '\n';
    return 1;
  }

  return 0;
}

auto AttachSession(const DaemonEndpoint& endpoint, const std::string& session_id,
                   const vibe::session::ControllerKind controller_kind) -> int {
  if (controller_kind == vibe::session::ControllerKind::Host && IsLocalEndpointHost(endpoint.host)) {
    const int local_attach_result = AttachSessionLocal(session_id);
    if (local_attach_result >= 0) {
      return local_attach_result;
    }
  }

  AttachTraceLogger trace_logger;
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

  ws.next_layer().set_option(tcp::no_delay(true), error_code);
  error_code.clear();

  ws.handshake(endpoint.host + ":" + ToStringPort(endpoint.port), BuildWebSocketTarget(session_id),
               error_code);
  if (error_code) {
    std::cerr << "websocket handshake failed: " << error_code.message() << '\n';
    return 1;
  }
  trace_logger.Log("ws.handshake");
  std::mutex stream_state_mutex;
  std::atomic<bool> stop_requested{false};
  std::atomic<int> shared_exit_code{0};
  std::atomic<bool> read_failed{false};
  std::string read_error_message;

  auto write_command = [&ws](const std::string& command) -> bool {
    boost::system::error_code write_error;
    ws.text(true);
    ws.write(asio::buffer(command), write_error);
    return !write_error;
  };

  auto write_raw_input = [&ws](const std::string_view input) -> bool {
    boost::system::error_code write_error;
    ws.text(false);
    ws.write(asio::buffer(input.data(), input.size()), write_error);
    return !write_error;
  };

  if (!write_command(BuildControlRequestCommand(controller_kind))) {
    std::cerr << "failed to request control\n";
    return 1;
  }
  trace_logger.Log("ws.write.control_request");
  SessionStreamState stream_state{
      .has_control = false,
      .control_request_pending = true,
      .active_controller_kind = "none",
      .active_controller_has_client = false,
  };
  ScopedTerminalEscapeReset terminal_escape_reset;
  ScopedRawTerminalMode raw_terminal_mode;
  ScopedSignalHandler window_resize_handler(SIGWINCH, HandleWindowResizeSignal);
  auto last_terminal_size = DetectTerminalSize();
  bool resize_sync_pending = true;
  bool stdin_open = true;

  std::thread reader_thread([&]() {
    beast::flat_buffer buffer;
    int reader_exit_code = 0;
    while (!stop_requested.load()) {
      boost::system::error_code read_error;
      ws.read(buffer, read_error);

      if (read_error == websocket::error::closed) {
        shared_exit_code.store(reader_exit_code);
        stop_requested.store(true);
        return;
      }
      if (read_error) {
        read_error_message = read_error.message();
        read_failed.store(true);
        stop_requested.store(true);
        return;
      }

      const std::string payload = beast::buffers_to_string(buffer.data());
      const bool got_text = ws.got_text();
      trace_logger.Log(got_text ? "ws.read.text" : "ws.read.binary", payload.size());
      buffer.consume(buffer.size());
      if (got_text) {
        std::lock_guard<std::mutex> lock(stream_state_mutex);
        if (!HandleServerMessage(payload, controller_kind, stream_state, reader_exit_code)) {
          shared_exit_code.store(reader_exit_code);
          stop_requested.store(true);
          return;
        }
      } else {
        trace_logger.Log("stdout.write", payload.size());
        WriteAllToStdout(payload);
      }
    }
  });

  while (true) {
    if (read_failed.load()) {
      stop_requested.store(true);
      break;
    }
    if (stop_requested.load()) {
      break;
    }

    SessionStreamState snapshot_state;
    {
      std::lock_guard<std::mutex> lock(stream_state_mutex);
      snapshot_state = stream_state;
    }

    if (controller_kind == vibe::session::ControllerKind::Host &&
        !snapshot_state.has_control &&
        !snapshot_state.control_request_pending &&
        snapshot_state.active_controller_kind == "host" &&
        !snapshot_state.active_controller_has_client) {
      const bool requested = write_command(BuildControlRequestCommand(controller_kind));
      if (!requested) {
        stop_requested.store(true);
        if (reader_thread.joinable()) {
          reader_thread.join();
        }
        std::cerr << "failed to reclaim host control\n";
        return 1;
      }
      {
        std::lock_guard<std::mutex> lock(stream_state_mutex);
        stream_state.control_request_pending = true;
      }
    }

    if (g_terminal_resize_pending != 0) {
      g_terminal_resize_pending = 0;
      const auto current_terminal_size = DetectTerminalSize();
      if (current_terminal_size != last_terminal_size) {
        last_terminal_size = current_terminal_size;
        resize_sync_pending = true;
      }
    }

    if (snapshot_state.has_control && resize_sync_pending) {
      const bool wrote_resize = write_command(BuildResizeCommand(last_terminal_size));
      if (!wrote_resize) {
        stop_requested.store(true);
        if (reader_thread.joinable()) {
          reader_thread.join();
        }
        std::cerr << "failed to send resize update\n";
        return 1;
      }
      trace_logger.Log("ws.write.resize");
      resize_sync_pending = false;
    }

    fd_set read_fds;
    FD_ZERO(&read_fds);
    int max_fd = -1;
    if (stdin_open) {
      FD_SET(STDIN_FILENO, &read_fds);
      max_fd = STDIN_FILENO;
    }

    timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = 20000;

    const int select_result =
        max_fd >= 0 ? select(max_fd + 1, &read_fds, nullptr, nullptr, &timeout) : 0;
    if (select_result < 0) {
      continue;
    }

    if (stdin_open && FD_ISSET(STDIN_FILENO, &read_fds)) {
        char input_buffer[1024];
        const ssize_t bytes_read = read(STDIN_FILENO, input_buffer, sizeof(input_buffer));
        if (bytes_read == 0) {
          const bool released = write_command(BuildReleaseControlCommand());
          static_cast<void>(released);
          trace_logger.Log("ws.write.release");
          stop_requested.store(true);
          break;
        } else if (bytes_read > 0) {
          trace_logger.Log("stdin.read", static_cast<std::size_t>(bytes_read));
          SessionStreamState input_state;
          {
            std::lock_guard<std::mutex> lock(stream_state_mutex);
            input_state = stream_state;
          }
          if (!input_state.has_control) {
            continue;
          }
          const bool wrote =
              write_raw_input(std::string_view(input_buffer, static_cast<std::size_t>(bytes_read)));
          if (!wrote) {
            stop_requested.store(true);
            if (reader_thread.joinable()) {
              reader_thread.join();
            }
            std::cerr << "failed to send terminal input\n";
            return 1;
          }
          trace_logger.Log("ws.write.input", static_cast<std::size_t>(bytes_read));
        }
    }
  }

  boost::system::error_code close_error;
  ws.close(websocket::close_code::normal, close_error);
  static_cast<void>(close_error);
  if (reader_thread.joinable()) {
    reader_thread.join();
  }
  if (read_failed.load()) {
    std::cerr << "websocket read failed: " << read_error_message << '\n';
    return 1;
  }
  return shared_exit_code.load();
}

auto ObserveSession(const DaemonEndpoint& endpoint, const std::string& session_id) -> int {
  AttachTraceLogger trace_logger;
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

  ws.next_layer().set_option(tcp::no_delay(true), error_code);
  error_code.clear();

  ws.handshake(endpoint.host + ":" + ToStringPort(endpoint.port), BuildWebSocketTarget(session_id),
               error_code);
  if (error_code) {
    std::cerr << "websocket handshake failed: " << error_code.message() << '\n';
    return 1;
  }
  trace_logger.Log("ws.handshake.observe");

  for (;;) {
    beast::flat_buffer buffer;
    ws.read(buffer, error_code);
    if (error_code == websocket::error::closed) {
      return 0;
    }
    if (error_code) {
      std::cerr << "websocket read failed: " << error_code.message() << '\n';
      return 1;
    }

    const std::string payload = beast::buffers_to_string(buffer.data());
    const bool got_text = ws.got_text();
    trace_logger.Log(got_text ? "ws.read.text.observe" : "ws.read.binary.observe", payload.size());

    if (!got_text) {
      trace_logger.Log("stdout.write.observe", payload.size());
      WriteAllToStdout(payload);
      continue;
    }

    boost::system::error_code parse_error;
    const json::value parsed = json::parse(payload, parse_error);
    if (parse_error || !parsed.is_object()) {
      continue;
    }

    const auto& object = parsed.as_object();
    const auto* type = object.if_contains("type");
    if (type == nullptr || !type->is_string()) {
      continue;
    }

    const std::string type_name = json::value_to<std::string>(*type);
    if (type_name == "terminal.output") {
      if (const auto* encoded = object.if_contains("dataBase64");
          encoded != nullptr && encoded->is_string()) {
        const auto decoded = DecodeBase64(json::value_to<std::string>(*encoded));
        if (decoded.has_value()) {
          WriteAllToStdout(*decoded);
        }
      }
      continue;
    }
    if (type_name == "session.exited") {
      return 0;
    }
  }
}

auto ObserveHubRelaySession(const std::string& hub_url, const std::string& bearer_token,
                            const std::string& host_id, const std::string& session_id) -> int {
  const auto channel_id = RequestHubRelayChannel(hub_url, bearer_token, host_id, session_id);
  if (!channel_id.has_value()) {
    return 1;
  }

  const auto parsed = ParseWebSocketBaseUrl(hub_url);
  if (!parsed.has_value()) {
    std::cerr << "invalid hub url: " << hub_url << '\n';
    return 1;
  }
  if (parsed->scheme != "ws") {
    std::cerr << "wss hub URLs are not supported by this relay proof client yet\n";
    return 1;
  }

  AttachTraceLogger trace_logger;
  asio::io_context io_context;
  tcp::resolver resolver(io_context);
  websocket::stream<tcp::socket> ws(io_context);
  boost::system::error_code error_code;

  const auto results = resolver.resolve(parsed->host, parsed->port, error_code);
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

  ws.next_layer().set_option(tcp::no_delay(true), error_code);
  error_code.clear();
  ws.set_option(websocket::stream_base::decorator(
      [&bearer_token](websocket::request_type& request) {
        request.set(boost::beast::http::field::authorization, "Bearer " + bearer_token);
      }));

  const std::string target = parsed->path_prefix + "/api/v1/relay/client/" + *channel_id;
  ws.handshake(parsed->host + ":" + parsed->port, target, error_code);
  if (error_code) {
    std::cerr << "websocket handshake failed: " << error_code.message() << '\n';
    return 1;
  }
  trace_logger.Log("ws.handshake.observe.relay");

  for (;;) {
    beast::flat_buffer buffer;
    ws.read(buffer, error_code);
    if (error_code == websocket::error::closed) {
      return 0;
    }
    if (error_code) {
      std::cerr << "websocket read failed: " << error_code.message() << '\n';
      return 1;
    }

    const std::string payload = beast::buffers_to_string(buffer.data());
    const bool got_text = ws.got_text();
    trace_logger.Log(got_text ? "ws.read.text.observe.relay" : "ws.read.binary.observe.relay",
                     payload.size());

    if (!got_text) {
      trace_logger.Log("stdout.write.observe.relay", payload.size());
      WriteAllToStdout(payload);
      continue;
    }

    boost::system::error_code parse_error;
    const json::value parsed_message = json::parse(payload, parse_error);
    if (parse_error || !parsed_message.is_object()) {
      continue;
    }

    const auto& object = parsed_message.as_object();
    const auto* type = object.if_contains("type");
    if (type == nullptr || !type->is_string()) {
      continue;
    }

    const std::string type_name = json::value_to<std::string>(*type);
    if (type_name == "terminal.output") {
      if (const auto* encoded = object.if_contains("dataBase64");
          encoded != nullptr && encoded->is_string()) {
        const auto decoded = DecodeBase64(json::value_to<std::string>(*encoded));
        if (decoded.has_value()) {
          WriteAllToStdout(*decoded);
        }
      }
      continue;
    }
    if (type_name == "session.exited") {
      return 0;
    }
  }
}

}  // namespace vibe::cli
