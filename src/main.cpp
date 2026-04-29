#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <system_error>
#include <pthread.h>
#include <signal.h>
#include <stdexcept>
#include <string>
#include <streambuf>
#include <thread>
#include <atomic>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include <boost/json.hpp>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include "vibe/cli/daemon_client.h"
#include "vibe/net/http_server.h"
#include "vibe/net/local_auth.h"
#include "vibe/session/env_config.h"
#include "vibe/session/launch_spec.h"
#include "vibe/session/pty_process_factory.h"
#include "vibe/session/session_record.h"
#include "vibe/session/session_runtime.h"
#include "vibe/session/session_types.h"
#include "vibe/store/file_stores.h"

namespace {

namespace json = boost::json;

#ifndef SENTRITS_DEFAULT_PACKAGED_WEB_ROOT
#define SENTRITS_DEFAULT_PACKAGED_WEB_ROOT ""
#endif

volatile sig_atomic_t g_local_terminal_resize_pending = 0;

void HandleLocalWindowResizeSignal(int /*signal_number*/) { g_local_terminal_resize_pending = 1; }

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
    raw_attributes.c_lflag &= static_cast<tcflag_t>(~(ECHO | ICANON | IEXTEN | ISIG));
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

 private:
  termios original_attributes_{};
  bool active_{false};
};

void RestoreTerminalEscapeModes() {
  if (!isatty(STDOUT_FILENO)) {
    return;
  }

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

auto MakeLocalSessionMetadata() -> vibe::session::SessionMetadata {
  const auto session_id = vibe::session::SessionId::TryCreate("local_pty");
  if (!session_id.has_value()) {
    throw std::runtime_error("failed to create local session id");
  }

  return vibe::session::SessionMetadata{
      .id = *session_id,
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = std::filesystem::current_path().string(),
      .title = "local-pty",
      .status = vibe::session::SessionStatus::Created,
      .conversation_id = std::nullopt,
      .group_tags = {},
  };
}

auto RunLocalPty(const std::vector<std::string>& command) -> int {
  using vibe::session::LaunchSpec;
  using vibe::session::OutputSlice;
  using vibe::session::ProviderType;
  using vibe::session::SessionRecord;
  using vibe::session::SessionRuntime;
  using vibe::session::SessionStatus;
  using vibe::session::TerminalSize;

  const auto metadata = MakeLocalSessionMetadata();
  const std::vector<std::string> arguments(command.begin() + 1, command.end());
  const TerminalSize initial_terminal_size = DetectTerminalSize();
  const LaunchSpec launch_spec{
      .provider = ProviderType::Codex,
      .executable = command.front(),
      .arguments = arguments,
      .effective_environment = {},
      .working_directory = metadata.workspace_root,
      .terminal_size = initial_terminal_size.columns > 0 && initial_terminal_size.rows > 0
                           ? initial_terminal_size
                           : TerminalSize{.columns = 120, .rows = 40},
  };

  const vibe::session::PtyPlatformSupport platform_support =
      vibe::session::DetectPtyPlatformSupport();
  auto process = vibe::session::CreatePlatformPtyProcess();
  if (!platform_support.supports_native_pty || process == nullptr) {
    std::cerr << "local-pty is unavailable on " << platform_support.platform_name << ": "
              << platform_support.detail << "\n";
    return 1;
  }

  SessionRuntime runtime(SessionRecord(metadata), launch_spec, *process, 64U * 1024U);
  if (!runtime.Start()) {
    std::cerr << "failed to start local PTY session\n";
    return 1;
  }

  ScopedTerminalEscapeReset terminal_escape_reset;
  ScopedRawTerminalMode raw_terminal_mode;
  ScopedSignalHandler window_resize_handler(SIGWINCH, HandleLocalWindowResizeSignal);
  std::uint64_t next_output_sequence = 1;
  auto last_terminal_size = launch_spec.terminal_size;
  bool resize_sync_pending = true;
  bool stdin_open = true;
  const std::optional<int> readable_fd = runtime.readable_fd();

  while (true) {
    if (g_local_terminal_resize_pending != 0) {
      g_local_terminal_resize_pending = 0;
      const auto current_terminal_size = DetectTerminalSize();
      if (current_terminal_size.columns > 0 && current_terminal_size.rows > 0 &&
          current_terminal_size != last_terminal_size) {
        last_terminal_size = current_terminal_size;
        resize_sync_pending = true;
      }
    }

    if (resize_sync_pending) {
      const bool resized = runtime.ResizeTerminal(last_terminal_size);
      static_cast<void>(resized);
      resize_sync_pending = false;
    }

    const OutputSlice output = runtime.output_buffer().SliceFromSequence(next_output_sequence);
    if (!output.data.empty()) {
      std::cout << output.data << std::flush;
      next_output_sequence = output.seq_end + 1;
    }

    const SessionStatus status = runtime.record().metadata().status;
    if (status == SessionStatus::Exited) {
      return 0;
    }
    if (status == SessionStatus::Error) {
      return 1;
    }

    if (!stdin_open) {
      if (readable_fd.has_value()) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(*readable_fd, &read_fds);
        const int ready = select(*readable_fd + 1, &read_fds, nullptr, nullptr, nullptr);
        if (ready > 0 && FD_ISSET(*readable_fd, &read_fds)) {
          runtime.PollOnce(0);
        }
      } else {
        runtime.PollOnce(10);
      }
      continue;
    }

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(STDIN_FILENO, &read_fds);
    int max_fd = STDIN_FILENO;
    if (readable_fd.has_value()) {
      FD_SET(*readable_fd, &read_fds);
      if (*readable_fd > max_fd) {
        max_fd = *readable_fd;
      }
    }

    const int select_result = select(max_fd + 1, &read_fds, nullptr, nullptr, nullptr);
    if (select_result <= 0) {
      continue;
    }

    if (readable_fd.has_value() && FD_ISSET(*readable_fd, &read_fds)) {
      runtime.PollOnce(0);
    }

    if (FD_ISSET(STDIN_FILENO, &read_fds)) {
      char buffer[1024];
      const ssize_t bytes_read = read(STDIN_FILENO, buffer, sizeof(buffer));
      if (bytes_read == 0) {
        stdin_open = false;
        continue;
      }
      if (bytes_read < 0) {
        continue;
      }

      const std::string input(buffer, static_cast<std::size_t>(bytes_read));
      const bool wrote_input = runtime.WriteInput(input);
      static_cast<void>(wrote_input);
    }
  }
}

auto HomeDirectory() -> std::optional<std::filesystem::path> {
  if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
    return std::filesystem::path(home);
  }
  return std::nullopt;
}

auto ResolveExecutablePath() -> std::optional<std::filesystem::path> {
#ifdef __APPLE__
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  if (size == 0) {
    return std::nullopt;
  }
  std::string buffer(size, '\0');
  if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
    return std::nullopt;
  }
  return std::filesystem::weakly_canonical(std::filesystem::path(buffer.c_str()));
#else
  std::vector<char> buffer(1024);
  while (true) {
    const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size());
    if (length < 0) {
      return std::nullopt;
    }
    if (static_cast<std::size_t>(length) < buffer.size()) {
      return std::filesystem::weakly_canonical(
          std::filesystem::path(std::string(buffer.data(), static_cast<std::size_t>(length))));
    }
    buffer.resize(buffer.size() * 2U);
  }
#endif
}

auto ResolveSiblingPackagedWebRoot(const std::filesystem::path& executable_path) -> std::filesystem::path {
#ifdef __APPLE__
  return executable_path.parent_path() / ".." / "lib" / "sentrits" / "www";
#else
  return executable_path.parent_path() / ".." / "lib" / "sentrits" / "www";
#endif
}

auto ResolvePackagedWebRootForServices() -> std::filesystem::path {
  if (const char* web_root = std::getenv("SENTRITS_WEB_ROOT"); web_root != nullptr && *web_root != '\0') {
    return std::filesystem::path(web_root);
  }

  if (const auto executable_path = ResolveExecutablePath(); executable_path.has_value()) {
    const auto sibling_root = ResolveSiblingPackagedWebRoot(*executable_path).lexically_normal();
    if (std::filesystem::exists(sibling_root)) {
      return sibling_root;
    }
  }

  constexpr std::string_view compiled_root = SENTRITS_DEFAULT_PACKAGED_WEB_ROOT;
  if (!compiled_root.empty()) {
    return std::filesystem::path(compiled_root);
  }

  return std::filesystem::path();
}

#ifdef __APPLE__
auto EscapeLaunchdString(const std::string& value) -> std::string {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char ch : value) {
    switch (ch) {
      case '&':
        escaped += "&amp;";
        break;
      case '<':
        escaped += "&lt;";
        break;
      case '>':
        escaped += "&gt;";
        break;
      default:
        escaped.push_back(ch);
        break;
    }
  }
  return escaped;
}

auto BuildLaunchdAgentContent(const std::filesystem::path& executable_path,
                              const std::filesystem::path& web_root) -> std::string {
  std::ostringstream output;
  output << R"(<?xml version="1.0" encoding="UTF-8"?>)"
         << "\n"
         << R"(<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">)"
         << "\n"
         << R"(<plist version="1.0">)"
         << "\n"
         << "<dict>\n"
         << "  <key>Label</key>\n"
         << "  <string>io.sentrits.agent</string>\n"
         << "  <key>ProgramArguments</key>\n"
         << "  <array>\n"
         << "    <string>" << EscapeLaunchdString(executable_path.string()) << "</string>\n"
         << "    <string>serve</string>\n"
         << "    <string>--admin-host</string>\n"
         << "    <string>127.0.0.1</string>\n"
         << "    <string>--remote-host</string>\n"
         << "    <string>0.0.0.0</string>\n"
         << "  </array>\n"
         << "  <key>EnvironmentVariables</key>\n"
         << "  <dict>\n"
         << "    <key>SENTRITS_WEB_ROOT</key>\n"
         << "    <string>" << EscapeLaunchdString(web_root.string()) << "</string>\n"
         << "  </dict>\n"
         << "  <key>KeepAlive</key>\n"
         << "  <true/>\n"
         << "  <key>RunAtLoad</key>\n"
         << "  <true/>\n"
         << "  <key>ProcessType</key>\n"
         << "  <string>Interactive</string>\n"
         << "</dict>\n"
         << "</plist>\n";
  return output.str();
}
#endif

#ifndef __APPLE__
auto BuildSystemdUserUnitContent(const std::filesystem::path& executable_path,
                                 const std::filesystem::path& web_root) -> std::string {
  std::ostringstream output;
  output << "[Unit]\n"
         << "Description=Sentrits user session daemon\n"
         << "After=default.target\n\n"
         << "[Service]\n"
         << "Type=simple\n"
         << "ExecStart=" << executable_path.string()
         << " serve --admin-host 127.0.0.1 --remote-host 0.0.0.0\n"
         << "Restart=on-failure\n"
         << "RestartSec=2\n";
  if (!web_root.empty()) {
    output << "Environment=SENTRITS_WEB_ROOT=" << web_root.string() << "\n";
  }
  output << "\n[Install]\n"
         << "WantedBy=default.target\n";
  return output.str();
}
#endif

auto WriteFileWithParents(const std::filesystem::path& path, const std::string& content) -> bool {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) {
    return false;
  }

  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    return false;
  }

  output << content;
  return output.good();
}

class TeeStreamBuf final : public std::streambuf {
 public:
  TeeStreamBuf(std::streambuf* primary, std::streambuf* secondary)
      : primary_(primary), secondary_(secondary) {}

 protected:
  auto overflow(int ch) -> int override {
    if (ch == EOF) {
      return sync() == 0 ? 0 : EOF;
    }

    const char character = static_cast<char>(ch);
    if (!WriteChar(primary_, character) || !WriteChar(secondary_, character)) {
      return EOF;
    }
    return ch;
  }

  auto sync() -> int override {
    const bool primary_ok = primary_ == nullptr || primary_->pubsync() == 0;
    const bool secondary_ok = secondary_ == nullptr || secondary_->pubsync() == 0;
    return primary_ok && secondary_ok ? 0 : -1;
  }

 private:
  static auto WriteChar(std::streambuf* buffer, const char character) -> bool {
    return buffer == nullptr || buffer->sputc(character) != EOF;
  }

  std::streambuf* primary_{nullptr};
  std::streambuf* secondary_{nullptr};
};

class RotatingFileStreamBuf final : public std::streambuf {
 public:
  RotatingFileStreamBuf(std::filesystem::path path, const std::uintmax_t max_bytes,
                        const std::size_t max_files)
      : path_(std::move(path)), max_bytes_(max_bytes), max_files_(std::max<std::size_t>(max_files, 1U)) {
    Open();
  }

 [[nodiscard]] auto available() const -> bool { return output_.is_open(); }

 protected:
  auto overflow(int ch) -> int override {
    if (ch == EOF) {
      return sync() == 0 ? 0 : EOF;
    }

    const char character = static_cast<char>(ch);
    return xsputn(&character, 1) == 1 ? ch : EOF;
  }

  auto xsputn(const char* data, std::streamsize count) -> std::streamsize override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!EnsureOpen() || data == nullptr || count <= 0) {
      return 0;
    }

    std::streamsize written_total = 0;
    while (written_total < count) {
      if (current_size_ >= max_bytes_ && !Rotate()) {
        break;
      }

      const std::uintmax_t remaining_before_rotate =
          current_size_ < max_bytes_ ? max_bytes_ - current_size_ : 0;
      const std::streamsize chunk =
          static_cast<std::streamsize>(std::min<std::uintmax_t>(
              static_cast<std::uintmax_t>(count - written_total),
              std::max<std::uintmax_t>(remaining_before_rotate, 1U)));

      output_.write(data + written_total, chunk);
      if (!output_.good()) {
        break;
      }

      written_total += chunk;
      current_size_ += static_cast<std::uintmax_t>(chunk);
    }

    return written_total;
  }

  auto sync() -> int override {
    std::lock_guard<std::mutex> lock(mutex_);
    return output_.is_open() && output_.flush() ? 0 : -1;
  }

 private:
  auto EnsureOpen() -> bool {
    if (output_.is_open()) {
      return true;
    }
    Open();
    return output_.is_open();
  }

  void Open() {
    std::error_code error;
    std::filesystem::create_directories(path_.parent_path(), error);
    if (error) {
      return;
    }

    output_.open(path_, std::ios::out | std::ios::app);
    if (!output_.is_open()) {
      return;
    }

    current_size_ = 0;
    if (std::filesystem::exists(path_, error) && !error) {
      current_size_ = std::filesystem::file_size(path_, error);
      if (error) {
        current_size_ = 0;
      }
    }
  }

  auto Rotate() -> bool {
    if (output_.is_open()) {
      output_.flush();
      output_.close();
    }

    std::error_code error;
    const auto oldest_path = RotatedPath(max_files_);
    std::filesystem::remove(oldest_path, error);
    error.clear();

    for (std::size_t index = max_files_; index > 1U; --index) {
      const auto source = RotatedPath(index - 1U);
      const auto target = RotatedPath(index);
      if (!std::filesystem::exists(source, error) || error) {
        error.clear();
        continue;
      }
      std::filesystem::rename(source, target, error);
      if (error) {
        return false;
      }
    }

    if (std::filesystem::exists(path_, error) && !error) {
      std::filesystem::rename(path_, RotatedPath(1U), error);
      if (error) {
        return false;
      }
    }

    current_size_ = 0;
    Open();
    return output_.is_open();
  }

  auto RotatedPath(const std::size_t index) const -> std::filesystem::path {
    return std::filesystem::path(path_.string() + "." + std::to_string(index));
  }

  std::filesystem::path path_;
  std::uintmax_t max_bytes_{0};
  std::size_t max_files_{1};
  std::ofstream output_;
  std::uintmax_t current_size_{0};
  std::mutex mutex_;
};

class ScopedServeLogMirror {
 public:
  explicit ScopedServeLogMirror(const std::filesystem::path& storage_root) {
    const auto log_path = vibe::net::DefaultServeLogPath(storage_root);
    rotating_buf_ =
        std::make_unique<RotatingFileStreamBuf>(log_path, 5U * 1024U * 1024U, 5U);
    if (rotating_buf_ == nullptr || !rotating_buf_->available()) {
      rotating_buf_.reset();
      return;
    }

    cout_buf_ = std::make_unique<TeeStreamBuf>(std::cout.rdbuf(), rotating_buf_.get());
    cerr_buf_ = std::make_unique<TeeStreamBuf>(std::cerr.rdbuf(), rotating_buf_.get());
    original_cout_ = std::cout.rdbuf(cout_buf_.get());
    original_cerr_ = std::cerr.rdbuf(cerr_buf_.get());
  }

  ScopedServeLogMirror(const ScopedServeLogMirror&) = delete;
  auto operator=(const ScopedServeLogMirror&) -> ScopedServeLogMirror& = delete;

  ~ScopedServeLogMirror() {
    if (original_cout_ != nullptr) {
      std::cout.rdbuf(original_cout_);
    }
    if (original_cerr_ != nullptr) {
      std::cerr.rdbuf(original_cerr_);
    }
  }

 private:
  std::unique_ptr<RotatingFileStreamBuf> rotating_buf_;
  std::unique_ptr<TeeStreamBuf> cout_buf_;
  std::unique_ptr<TeeStreamBuf> cerr_buf_;
  std::streambuf* original_cout_{nullptr};
  std::streambuf* original_cerr_{nullptr};
};

void PrintServiceUsage() {
  std::cout << "Usage:\n"
            << "  sentrits service install\n"
            << "  sentrits service print\n";
}

auto HandleServiceCommand(const int argc, char** argv) -> int {
  if (argc < 3) {
    PrintServiceUsage();
    return 1;
  }

  const std::string action = argv[2];
  const auto executable_path = ResolveExecutablePath();
  if (!executable_path.has_value()) {
    std::cerr << "failed to resolve sentrits executable path\n";
    return 1;
  }

  const auto home = HomeDirectory();
  if (!home.has_value()) {
    std::cerr << "HOME is not set\n";
    return 1;
  }

  const std::filesystem::path web_root = ResolvePackagedWebRootForServices();
#ifdef __APPLE__
  const std::filesystem::path service_path = *home / "Library" / "LaunchAgents" / "io.sentrits.agent.plist";
  const std::string content = BuildLaunchdAgentContent(*executable_path, web_root);
#else
  const std::filesystem::path service_path =
      *home / ".config" / "systemd" / "user" / "sentrits.service";
  const std::string content = BuildSystemdUserUnitContent(*executable_path, web_root);
#endif

  if (action == "print") {
    std::cout << "# path: " << service_path.string() << "\n" << content;
    return 0;
  }

  if (action != "install") {
    PrintServiceUsage();
    return 1;
  }

  if (!WriteFileWithParents(service_path, content)) {
    std::cerr << "failed to write service file: " << service_path.string() << "\n";
    return 1;
  }

  std::cout << "installed service file: " << service_path.string() << "\n";
#ifdef __APPLE__
  std::cout << "next:\n"
            << "  launchctl unload " << service_path.string() << " 2>/dev/null || true\n"
            << "  launchctl load " << service_path.string() << "\n";
#else
  std::cout << "next:\n"
            << "  systemctl --user daemon-reload\n"
            << "  systemctl --user enable sentrits.service\n"
            << "  systemctl --user start sentrits.service\n";
#endif
  return 0;
}

void PrintUsage() {
  std::cout << "Usage:\n"
            << "  sentrits serve [--admin-host HOST] [--admin-port PORT]"
               " [--remote-host HOST] [--remote-port PORT]"
               " [--remote-cert PATH] [--remote-key PATH]"
               " [--no-udp-discovery|--no-discovery]\n"
            << "  sentrits service install|print\n"
            << "  sentrits local-pty [command [args...]]\n"
            << "  sentrits session list [--host HOST] [--port PORT] [--json]\n"
            << "  sentrits session show [--host HOST] [--port PORT] [--json] <session-id>\n"
            << "  sentrits session start [--host HOST] [--port PORT] [--title TITLE]"
               " [--workspace PATH] [--provider codex|claude] [--record RECORD_ID]"
               " [--shell-command COMMAND] [--env-mode MODE] [-e KEY=VALUE]..."
               " [--env-file PATH] [--attach]\n"
            << "  sentrits records list [--host HOST] [--port PORT] [--json]\n"
            << "  sentrits records show [--host HOST] [--port PORT] [--json] <record-id>\n"
            << "  sentrits session attach [--host HOST] [--port PORT] <session-id>\n"
            << "  sentrits session observe [--host HOST] [--port PORT] <session-id>\n"
            << "  sentrits relay observe --hub-url URL --token TOKEN --host-id HOST_ID --session-id SESSION_ID\n"
            << "  sentrits session stop [--host HOST] [--port PORT] [--json] <session-id>\n"
            << "  sentrits session clear [--host HOST] [--port PORT] [--json]\n"
            << "  sentrits host status [--host HOST] [--port PORT] [--json]\n"
            << "  sentrits host set-name [--host HOST] [--port PORT] <display-name>\n"
            << "  sentrits host set-hub <hub-url> <hub-token>\n"
            << "  sentrits host clear-hub\n"
            << "  sentrits host set-provider-command [--host HOST] [--port PORT]"
               " --provider codex|claude -- <command> [args...]\n"
            << "  sentrits host clear-provider-command [--host HOST] [--port PORT]"
               " --provider codex|claude\n"
            << "\nCompatibility aliases:\n"
            << "  sentrits list\n"
            << "  sentrits session-start\n"
            << "  sentrits session-attach\n";
}

auto LoadConfiguredAdminEndpoint() -> vibe::cli::DaemonEndpoint {
  const vibe::store::FileHostConfigStore host_config_store{vibe::net::DefaultStorageRoot()};
  const auto host_identity = host_config_store.LoadHostIdentity();
  if (!host_identity.has_value()) {
    return {};
  }

  return vibe::cli::DaemonEndpoint{
      .host = host_identity->admin_host.empty() ? std::string(vibe::store::kDefaultAdminHost)
                                                : host_identity->admin_host,
      .port = host_identity->admin_port,
  };
}

auto PrettyPrintJson(const std::string& body) -> std::string {
  boost::system::error_code error_code;
  const json::value value = json::parse(body, error_code);
  if (error_code) {
    return body;
  }
  return json::serialize(value);
}

auto JsonString(const json::object& object, const std::string_view key) -> std::string {
  if (const auto* value = object.if_contains(key); value != nullptr && value->is_string()) {
    return json::value_to<std::string>(*value);
  }
  return {};
}

auto JsonUInt16(const json::object& object, const std::string_view key) -> std::optional<std::uint16_t> {
  if (const auto* value = object.if_contains(key); value != nullptr && value->is_int64()) {
    return static_cast<std::uint16_t>(value->as_int64());
  }
  return std::nullopt;
}

auto RecordToJsonValue(const vibe::cli::ListedRecord& record) -> json::value {
  json::object object;
  object["recordId"] = record.record_id;
  object["provider"] = record.provider;
  object["workspaceRoot"] = record.workspace_root;
  object["title"] = record.title;
  object["launchedAtUnixMs"] = record.launched_at_unix_ms;
  if (record.conversation_id.has_value()) {
    object["conversationId"] = *record.conversation_id;
  }
  if (!record.group_tags.empty()) {
    json::array group_tags;
    for (const auto& tag : record.group_tags) {
      group_tags.emplace_back(tag);
    }
    object["groupTags"] = std::move(group_tags);
  }
  if (record.command_argv.has_value()) {
    json::array command_argv;
    for (const auto& token : *record.command_argv) {
      command_argv.emplace_back(token);
    }
    object["commandArgv"] = std::move(command_argv);
  }
  if (record.command_shell.has_value()) {
    object["commandShell"] = *record.command_shell;
  }
  return object;
}

void PrintSessionListHuman(const std::vector<vibe::cli::ListedSession>& sessions) {
  for (const auto& session : sessions) {
    std::cout << session.session_id << '\t'
              << (session.title.empty() ? "(untitled)" : session.title) << '\t'
              << (session.activity_state.empty() ? session.status : session.activity_state);
    if (!session.interaction_kind.empty()) {
      std::cout << '\t' << session.interaction_kind;
    }
    if (!session.semantic_preview.empty()) {
      std::cout << '\t' << session.semantic_preview;
    }
    std::cout
              << '\n';
  }
}

auto RecordLaunchMode(const vibe::cli::ListedRecord& record) -> std::string {
  if (record.command_shell.has_value()) {
    return "shell";
  }
  if (record.command_argv.has_value() && !record.command_argv->empty()) {
    return "argv";
  }
  return "provider_default";
}

void PrintRecordListHuman(const std::vector<vibe::cli::ListedRecord>& records) {
  for (const auto& record : records) {
    std::cout << record.record_id << '\t'
              << RecordLaunchMode(record) << '\t'
              << record.provider << '\t'
              << record.workspace_root << '\t'
              << record.title << '\n';
  }
}

void PrintRecordHuman(const vibe::cli::ListedRecord& record) {
  std::cout << "Record ID:      " << record.record_id << '\n'
            << "Provider:       " << record.provider << '\n'
            << "Workspace:      " << record.workspace_root << '\n'
            << "Title:          " << record.title << '\n'
            << "Launched At:    " << record.launched_at_unix_ms << "ms\n"
            << "Launch Mode:    " << RecordLaunchMode(record) << '\n';
  if (record.conversation_id.has_value()) {
    std::cout << "Conversation:   " << *record.conversation_id << '\n';
  }
  if (!record.group_tags.empty()) {
    std::cout << "Group Tags:     ";
    for (std::size_t index = 0; index < record.group_tags.size(); ++index) {
      if (index != 0U) {
        std::cout << ", ";
      }
      std::cout << record.group_tags[index];
    }
    std::cout << '\n';
  }
  if (record.command_shell.has_value()) {
    std::cout << "Command Shell:  " << *record.command_shell << '\n';
  } else if (record.command_argv.has_value() && !record.command_argv->empty()) {
    std::cout << "Command Argv:   ";
    for (std::size_t index = 0; index < record.command_argv->size(); ++index) {
      if (index != 0U) {
        std::cout << ' ';
      }
      std::cout << (*record.command_argv)[index];
    }
    std::cout << '\n';
  }
}

auto PrintSessionSnapshotHuman(const std::string& body) -> bool {
  boost::system::error_code error_code;
  const json::value parsed = json::parse(body, error_code);
  if (error_code || !parsed.is_object()) {
    return false;
  }

  const auto& object = parsed.as_object();
  std::cout << "Session:        " << JsonString(object, "sessionId") << '\n'
            << "Title:          " << JsonString(object, "title") << '\n'
            << "Provider:       " << JsonString(object, "provider") << '\n'
            << "Workspace:      " << JsonString(object, "workspaceRoot") << '\n'
            << "Status:         " << JsonString(object, "status") << '\n'
            << "Supervision:    " << JsonString(object, "supervisionState") << '\n'
            << "Attention:      " << JsonString(object, "attentionState") << '\n'
            << "Reason:         " << JsonString(object, "attentionReason") << '\n'
            << "Interaction:    " << JsonString(object, "interactionKind") << '\n'
            << "Summary:        " << JsonString(object, "semanticPreview") << '\n'
            << "Controller:     " << JsonString(object, "controllerKind") << '\n';

  if (const auto* signals = object.if_contains("signals"); signals != nullptr && signals->is_object()) {
    const auto& signal_object = signals->as_object();
    const auto pty_cols = JsonUInt16(signal_object, "ptyCols");
    const auto pty_rows = JsonUInt16(signal_object, "ptyRows");
    if (pty_cols.has_value() && pty_rows.has_value()) {
      std::cout << "PTY Size:       " << *pty_cols << " x " << *pty_rows << '\n';
    }
  }

  if (const auto* git = object.if_contains("git"); git != nullptr && git->is_object()) {
    const auto& git_object = git->as_object();
    const std::string branch = JsonString(git_object, "branch");
    if (!branch.empty()) {
      std::cout << "Branch:         " << branch << '\n';
    }
  }

  return true;
}

auto PrintHostStatusHuman(const std::string& body) -> bool {
  boost::system::error_code error_code;
  const json::value parsed = json::parse(body, error_code);
  if (error_code || !parsed.is_object()) {
    return false;
  }

  const auto& object = parsed.as_object();
  std::cout << "Host ID:        " << JsonString(object, "hostId") << '\n'
            << "Display Name:   " << JsonString(object, "displayName") << '\n'
            << "Admin Listen:   " << JsonString(object, "adminHost") << ':'
            << JsonUInt16(object, "adminPort").value_or(0) << '\n'
            << "Remote Listen:  " << JsonString(object, "remoteHost") << ':'
            << JsonUInt16(object, "remotePort").value_or(0) << '\n'
            << "Version:        " << JsonString(object, "version") << '\n';
  if (const auto* tls = object.if_contains("tls"); tls != nullptr && tls->is_object()) {
    const auto& tls_object = tls->as_object();
    std::cout << "TLS:            " << JsonString(tls_object, "mode") << '\n';
  }
  return true;
}

struct ParsedCommandOptions {
  vibe::cli::DaemonEndpoint endpoint;
  bool json_output{false};
  bool attach_after_create{false};
  std::optional<std::string> title;
  std::optional<std::string> workspace_root;
  std::optional<std::string> record_id;
  std::optional<std::string> shell_command;
  std::optional<vibe::session::ProviderType> provider;
  std::vector<std::string> positionals;
  // Environment model options.
  std::optional<vibe::session::EnvMode> env_mode;
  std::unordered_map<std::string, std::string> environment_overrides;
  std::optional<std::string> env_file_path;
};

auto ParseProviderOption(const std::string& value) -> std::optional<vibe::session::ProviderType> {
  if (value == "codex") {
    return vibe::session::ProviderType::Codex;
  }
  if (value == "claude") {
    return vibe::session::ProviderType::Claude;
  }
  return std::nullopt;
}

void ConsumeImplicitProviderPositional(ParsedCommandOptions& options) {
  if (options.provider.has_value() || options.positionals.empty()) {
    return;
  }

  const auto provider = ParseProviderOption(options.positionals.front());
  if (!provider.has_value()) {
    return;
  }

  options.provider = *provider;
  options.positionals.erase(options.positionals.begin());
}

struct RelayObserveOptions {
  std::string hub_url;
  std::string token;
  std::string host_id;
  std::string session_id;
};

auto ParseRelayObserveOptions(const int argc, char** argv, int start_index)
    -> std::optional<RelayObserveOptions> {
  RelayObserveOptions options;
  int index = start_index;
  while (index < argc) {
    const std::string argument = argv[index];
    if (argument == "--hub-url" && index + 1 < argc) {
      options.hub_url = argv[index + 1];
      index += 2;
      continue;
    }
    if (argument == "--token" && index + 1 < argc) {
      options.token = argv[index + 1];
      index += 2;
      continue;
    }
    if (argument == "--host-id" && index + 1 < argc) {
      options.host_id = argv[index + 1];
      index += 2;
      continue;
    }
    if (argument == "--session-id" && index + 1 < argc) {
      options.session_id = argv[index + 1];
      index += 2;
      continue;
    }
    return std::nullopt;
  }
  if (options.hub_url.empty() || options.token.empty() ||
      options.host_id.empty() || options.session_id.empty()) {
    return std::nullopt;
  }
  return options;
}

auto ParseCommandOptions(const int argc, char** argv, int start_index,
                         vibe::cli::DaemonEndpoint default_endpoint) -> std::optional<ParsedCommandOptions> {
  ParsedCommandOptions options;
  options.endpoint = default_endpoint;
  int index = start_index;
  while (index < argc) {
    const std::string argument = argv[index];
    if (argument == "--host" && index + 1 < argc) {
      options.endpoint.host = argv[index + 1];
      index += 2;
      continue;
    }
    if (argument == "--port" && index + 1 < argc) {
      options.endpoint.port = static_cast<std::uint16_t>(std::stoi(argv[index + 1]));
      index += 2;
      continue;
    }
    if (argument == "--json") {
      options.json_output = true;
      index += 1;
      continue;
    }
    if (argument == "--attach") {
      options.attach_after_create = true;
      index += 1;
      continue;
    }
    if (argument == "--title" && index + 1 < argc) {
      options.title = argv[index + 1];
      index += 2;
      continue;
    }
    if (argument == "--workspace" && index + 1 < argc) {
      options.workspace_root = argv[index + 1];
      index += 2;
      continue;
    }
    if (argument == "--record" && index + 1 < argc) {
      options.record_id = argv[index + 1];
      index += 2;
      continue;
    }
    if (argument == "--shell-command" && index + 1 < argc) {
      options.shell_command = argv[index + 1];
      index += 2;
      continue;
    }
    if (argument == "--provider" && index + 1 < argc) {
      const auto provider = ParseProviderOption(argv[index + 1]);
      if (!provider.has_value()) {
        return std::nullopt;
      }
      options.provider = *provider;
      index += 2;
      continue;
    }
    if (argument == "--env-mode" && index + 1 < argc) {
      const auto env_mode = vibe::session::ParseEnvMode(argv[index + 1]);
      if (!env_mode.has_value()) {
        return std::nullopt;
      }
      options.env_mode = *env_mode;
      index += 2;
      continue;
    }
    if ((argument == "-e" || argument == "--env") && index + 1 < argc) {
      const std::string kv = argv[index + 1];
      const auto eq_pos = kv.find('=');
      if (eq_pos == std::string::npos || eq_pos == 0) {
        return std::nullopt;
      }
      options.environment_overrides[kv.substr(0, eq_pos)] = kv.substr(eq_pos + 1);
      index += 2;
      continue;
    }
    if (argument == "--env-file" && index + 1 < argc) {
      options.env_file_path = argv[index + 1];
      index += 2;
      continue;
    }
    if (!argument.empty() && argument[0] == '-') {
      return std::nullopt;
    }
    options.positionals.emplace_back(argument);
    index += 1;
  }
  return options;
}

}  // namespace

auto main(const int argc, char** argv) -> int {
  if (argc >= 2 && std::string(argv[1]) == "service") {
    return HandleServiceCommand(argc, argv);
  }

  if (argc >= 2 && std::string(argv[1]) == "serve") {
    const auto storage_root = vibe::net::DefaultStorageRoot();
    ScopedServeLogMirror serve_log_mirror(storage_root);
    std::string admin_host = std::string(vibe::store::kDefaultAdminHost);
    std::uint16_t admin_port = vibe::store::kDefaultAdminPort;
    std::string remote_host = std::string(vibe::store::kDefaultRemoteHost);
    std::uint16_t remote_port = vibe::store::kDefaultRemotePort;
    std::optional<vibe::net::RemoteTlsFiles> remote_tls_override = std::nullopt;
    bool enable_discovery = true;
    bool admin_host_explicit = false;
    bool admin_port_explicit = false;
    bool remote_host_explicit = false;
    bool remote_port_explicit = false;

    int index = 2;
    while (index < argc) {
      const std::string argument = argv[index];
      if (argument == "--admin-host" && index + 1 < argc) {
        admin_host = argv[index + 1];
        admin_host_explicit = true;
        index += 2;
        continue;
      }
      if (argument == "--admin-port" && index + 1 < argc) {
        admin_port = static_cast<std::uint16_t>(std::stoi(argv[index + 1]));
        admin_port_explicit = true;
        index += 2;
        continue;
      }
      if (argument == "--remote-host" && index + 1 < argc) {
        remote_host = argv[index + 1];
        remote_host_explicit = true;
        index += 2;
        continue;
      }
      if (argument == "--remote-port" && index + 1 < argc) {
        remote_port = static_cast<std::uint16_t>(std::stoi(argv[index + 1]));
        remote_port_explicit = true;
        index += 2;
        continue;
      }
      if (argument == "--remote-cert" && index + 1 < argc) {
        if (!remote_tls_override.has_value()) {
          remote_tls_override = vibe::net::RemoteTlsFiles{};
        }
        remote_tls_override->certificate_pem_path = argv[index + 1];
        index += 2;
        continue;
      }
      if (argument == "--remote-key" && index + 1 < argc) {
        if (!remote_tls_override.has_value()) {
          remote_tls_override = vibe::net::RemoteTlsFiles{};
        }
        remote_tls_override->private_key_pem_path = argv[index + 1];
        index += 2;
        continue;
      }
      if (argument == "--no-udp-discovery" || argument == "--no-discovery") {
        enable_discovery = false;
        index += 1;
        continue;
      }
      if (argc - index == 2) {
        remote_host = argv[index];
        remote_port = static_cast<std::uint16_t>(std::stoi(argv[index + 1]));
        break;
      }
      std::cerr << "invalid serve arguments\n";
      PrintUsage();
      return 1;
    }

    std::string hub_url;
    std::string hub_token;
    {
      const vibe::store::FileHostConfigStore host_config_store{storage_root};
      if (const auto host_identity = host_config_store.LoadHostIdentity(); host_identity.has_value()) {
        if (!admin_host_explicit) {
          admin_host = host_identity->admin_host;
        }
        if (!admin_port_explicit) {
          admin_port = host_identity->admin_port;
        }
        if (!remote_host_explicit) {
          remote_host = host_identity->remote_host;
        }
        if (!remote_port_explicit) {
          remote_port = host_identity->remote_port;
        }
        if (host_identity->hub_url.has_value()) {
          hub_url = *host_identity->hub_url;
        }
        if (host_identity->hub_token.has_value()) {
          hub_token = *host_identity->hub_token;
        }
      }
    }

    vibe::net::HttpServer server(admin_host, admin_port, remote_host, remote_port,
                                 remote_tls_override, enable_discovery);
    server.EnableHubIntegration(hub_url, hub_token);
    server.EnableHubControlChannel(hub_url, hub_token);
    sigset_t signal_set;
    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGHUP);
    sigaddset(&signal_set, SIGINT);
    sigaddset(&signal_set, SIGQUIT);
    sigaddset(&signal_set, SIGTERM);

    sigset_t previous_signal_set;
    if (pthread_sigmask(SIG_BLOCK, &signal_set, &previous_signal_set) != 0) {
      std::cerr << "failed to install daemon signal mask\n";
      return 1;
    }

    std::atomic<bool> stop_requested{false};
    std::thread signal_thread([&server, &signal_set, &stop_requested]() {
      int signal_number = 0;
      if (sigwait(&signal_set, &signal_number) == 0) {
        stop_requested.store(true);
        server.Stop();
      }
    });

    const int exit_code = server.Run() ? 0 : 1;
    if (!stop_requested.exchange(true) && signal_thread.joinable()) {
      const int wake_result = pthread_kill(signal_thread.native_handle(), SIGTERM);
      static_cast<void>(wake_result);
    }
    if (signal_thread.joinable()) {
      signal_thread.join();
    }

    const int restore_result = pthread_sigmask(SIG_SETMASK, &previous_signal_set, nullptr);
    static_cast<void>(restore_result);
    return exit_code;
  }

  if (argc >= 2 && std::string(argv[1]) == "local-pty") {
    std::vector<std::string> command;
    for (int index = 2; index < argc; ++index) {
      command.emplace_back(argv[index]);
    }
    if (command.empty()) {
      command = {"/bin/sh"};
    }
    return RunLocalPty(command);
  }

  const std::string command = argc >= 2 ? std::string(argv[1]) : "";
  if (command == "list") {
    if (auto options = ParseCommandOptions(argc, argv, 2, LoadConfiguredAdminEndpoint());
        options.has_value()) {
      const auto sessions = vibe::cli::ListSessions(options->endpoint);
      if (!sessions.has_value()) {
        std::cerr << "failed to list sessions via daemon at " << options->endpoint.host << ":"
                  << options->endpoint.port << '\n';
        return 1;
      }
      if (options->json_output) {
        json::array array;
        for (const auto& session : *sessions) {
          json::object object;
          object["sessionId"] = session.session_id;
          object["title"] = session.title;
          object["activityState"] = session.activity_state;
          object["status"] = session.status;
          array.push_back(std::move(object));
        }
        std::cout << json::serialize(array) << '\n';
      } else {
        PrintSessionListHuman(*sessions);
      }
      return 0;
    }
    PrintUsage();
    return 1;
  }

  if (command == "session-start") {
    if (auto options = ParseCommandOptions(argc, argv, 2, LoadConfiguredAdminEndpoint());
        options.has_value()) {
      ConsumeImplicitProviderPositional(*options);
      if (!options->positionals.empty()) {
        std::cerr << "unexpected positional arguments for session-start\n";
        PrintUsage();
        return 1;
      }
      const std::string title = options->title.value_or("host-session");
      const auto created = vibe::cli::CreateSessionWithDetail(
          options->endpoint,
          vibe::cli::CreateSessionRequest{
              .provider = options->provider,
              .workspace_root = options->workspace_root.value_or(std::filesystem::current_path().string()),
              .title = title,
              .record_id = options->record_id,
              .command_argv = std::nullopt,
              .command_shell = options->shell_command,
              .env_mode = options->env_mode,
              .environment_overrides = options->environment_overrides,
              .env_file_path = options->env_file_path,
          });
      if (!created.session_id.has_value()) {
        std::cerr << "failed to create session via daemon at " << options->endpoint.host << ":"
                  << options->endpoint.port;
        if (!created.error_message.empty()) {
          std::cerr << ": " << created.error_message;
        }
        std::cerr << '\n';
        return 1;
      }
      std::cerr << "session " << *created.session_id << " created\n";
      return vibe::cli::AttachSession(options->endpoint, *created.session_id, vibe::session::ControllerKind::Host);
    }
    PrintUsage();
    return 1;
  }

  if (command == "session-attach") {
    if (auto options = ParseCommandOptions(argc, argv, 2, LoadConfiguredAdminEndpoint());
        options.has_value() && !options->positionals.empty()) {
      return vibe::cli::AttachSession(options->endpoint, options->positionals.front(),
                                      vibe::session::ControllerKind::Host);
    }
    std::cerr << "session id required\n";
    return 1;
  }

  if (command == "relay" && argc >= 3) {
    const std::string subcommand = argv[2];
    if (subcommand == "observe") {
      const auto options = ParseRelayObserveOptions(argc, argv, 3);
      if (!options.has_value()) {
        PrintUsage();
        return 1;
      }
      return vibe::cli::ObserveHubRelaySession(
          options->hub_url, options->token, options->host_id, options->session_id);
    }
  }

  if (command == "session" && argc >= 3) {
    const std::string subcommand = argv[2];
    if (subcommand == "list") {
      const auto options = ParseCommandOptions(argc, argv, 3, LoadConfiguredAdminEndpoint());
      if (!options.has_value()) {
        PrintUsage();
        return 1;
      }
      const auto sessions = vibe::cli::ListSessions(options->endpoint);
      if (!sessions.has_value()) {
        std::cerr << "failed to list sessions via daemon at " << options->endpoint.host << ":"
                  << options->endpoint.port << '\n';
        return 1;
      }
      if (options->json_output) {
        json::array array;
        for (const auto& session : *sessions) {
          json::object object;
          object["sessionId"] = session.session_id;
          object["title"] = session.title;
          object["activityState"] = session.activity_state;
          object["status"] = session.status;
          array.push_back(std::move(object));
        }
        std::cout << json::serialize(array) << '\n';
      } else {
        PrintSessionListHuman(*sessions);
      }
      return 0;
    }

    if (subcommand == "show") {
      const auto options = ParseCommandOptions(argc, argv, 3, LoadConfiguredAdminEndpoint());
      if (!options.has_value() || options->positionals.empty()) {
        PrintUsage();
        return 1;
      }
      const auto snapshot = vibe::cli::GetSessionSnapshot(options->endpoint, options->positionals.front());
      if (!snapshot.has_value()) {
        std::cerr << "failed to show session via daemon at " << options->endpoint.host << ":"
                  << options->endpoint.port << '\n';
        return 1;
      }
      if (options->json_output || !PrintSessionSnapshotHuman(*snapshot)) {
        std::cout << PrettyPrintJson(*snapshot) << '\n';
      }
      return 0;
    }

    if (subcommand == "start") {
      auto options = ParseCommandOptions(argc, argv, 3, LoadConfiguredAdminEndpoint());
      if (!options.has_value()) {
        PrintUsage();
        return 1;
      }
      ConsumeImplicitProviderPositional(*options);
      if (!options->positionals.empty()) {
        std::cerr << "unexpected positional arguments for session start\n";
        PrintUsage();
        return 1;
      }
      const std::string title = options->title.value_or("session");
      const auto created = vibe::cli::CreateSessionWithDetail(
          options->endpoint,
          vibe::cli::CreateSessionRequest{
              .provider = options->provider,
              .workspace_root = options->workspace_root.value_or(std::filesystem::current_path().string()),
              .title = title,
              .record_id = options->record_id,
              .command_argv = std::nullopt,
              .command_shell = options->shell_command,
              .env_mode = options->env_mode,
              .environment_overrides = options->environment_overrides,
              .env_file_path = options->env_file_path,
          });
      if (!created.session_id.has_value()) {
        std::cerr << "failed to create session via daemon at " << options->endpoint.host << ":"
                  << options->endpoint.port;
        if (!created.error_message.empty()) {
          std::cerr << ": " << created.error_message;
        }
        std::cerr << '\n';
        return 1;
      }
      if (options->json_output) {
        json::object object;
        object["sessionId"] = *created.session_id;
        object["title"] = title;
        std::cout << json::serialize(object) << '\n';
      } else {
        std::cout << "session " << *created.session_id << " created\n";
      }
      if (options->attach_after_create) {
        return vibe::cli::AttachSession(options->endpoint, *created.session_id, vibe::session::ControllerKind::Host);
      }
      return 0;
    }

    if (subcommand == "attach") {
      const auto options = ParseCommandOptions(argc, argv, 3, LoadConfiguredAdminEndpoint());
      if (!options.has_value() || options->positionals.empty()) {
        std::cerr << "session id required\n";
        return 1;
      }
      return vibe::cli::AttachSession(options->endpoint, options->positionals.front(),
                                      vibe::session::ControllerKind::Host);
    }

    if (subcommand == "observe") {
      const auto options = ParseCommandOptions(argc, argv, 3, LoadConfiguredAdminEndpoint());
      if (!options.has_value() || options->positionals.empty()) {
        std::cerr << "session id required\n";
        return 1;
      }
      return vibe::cli::ObserveSession(options->endpoint, options->positionals.front());
    }

    if (subcommand == "stop") {
      const auto options = ParseCommandOptions(argc, argv, 3, LoadConfiguredAdminEndpoint());
      if (!options.has_value() || options->positionals.empty()) {
        std::cerr << "session id required\n";
        return 1;
      }
      const auto result = vibe::cli::StopSession(options->endpoint, options->positionals.front());
      if (!result.has_value()) {
        std::cerr << "failed to stop session via daemon at " << options->endpoint.host << ":"
                  << options->endpoint.port << '\n';
        return 1;
      }
      if (options->json_output) {
        std::cout << PrettyPrintJson(*result) << '\n';
      } else {
        std::cout << "session " << options->positionals.front() << " stopped\n";
      }
      return 0;
    }

    if (subcommand == "env") {
      const auto options = ParseCommandOptions(argc, argv, 3, LoadConfiguredAdminEndpoint());
      if (!options.has_value() || options->positionals.empty()) {
        std::cerr << "session id required\n";
        return 1;
      }
      const auto result = vibe::cli::GetSessionEnv(options->endpoint, options->positionals.front());
      if (!result.has_value()) {
        std::cerr << "failed to get session env from daemon at " << options->endpoint.host << ":"
                  << options->endpoint.port << '\n';
        return 1;
      }
      std::cout << PrettyPrintJson(*result) << '\n';
      return 0;
    }

    if (subcommand == "clear") {
      const auto options = ParseCommandOptions(argc, argv, 3, LoadConfiguredAdminEndpoint());
      if (!options.has_value()) {
        PrintUsage();
        return 1;
      }
      const auto result = vibe::cli::ClearInactiveSessions(options->endpoint);
      if (!result.has_value()) {
        std::cerr << "failed to clear inactive sessions via daemon at " << options->endpoint.host << ":"
                  << options->endpoint.port << '\n';
        return 1;
      }
      if (options->json_output) {
        std::cout << PrettyPrintJson(*result) << '\n';
      } else {
        std::cout << "cleared inactive sessions\n";
      }
      return 0;
    }
  }

  if (command == "records" && argc >= 3) {
    const std::string subcommand = argv[2];
    if (subcommand == "list") {
      const auto options = ParseCommandOptions(argc, argv, 3, LoadConfiguredAdminEndpoint());
      if (!options.has_value()) {
        PrintUsage();
        return 1;
      }
      const auto records = vibe::cli::ListRecords(options->endpoint);
      if (!records.has_value()) {
        std::cerr << "failed to list launch records via daemon at " << options->endpoint.host << ":"
                  << options->endpoint.port << '\n';
        return 1;
      }
      if (options->json_output) {
        json::array array;
        for (const auto& record : *records) {
          array.emplace_back(RecordToJsonValue(record));
        }
        std::cout << json::serialize(array) << '\n';
      } else {
        PrintRecordListHuman(*records);
      }
      return 0;
    }

    if (subcommand == "show") {
      const auto options = ParseCommandOptions(argc, argv, 3, LoadConfiguredAdminEndpoint());
      if (!options.has_value() || options->positionals.empty()) {
        PrintUsage();
        return 1;
      }
      const auto records = vibe::cli::ListRecords(options->endpoint);
      if (!records.has_value()) {
        std::cerr << "failed to list launch records via daemon at " << options->endpoint.host << ":"
                  << options->endpoint.port << '\n';
        return 1;
      }
      const auto it = std::find_if(records->begin(), records->end(),
                                   [&](const vibe::cli::ListedRecord& record) {
                                     return record.record_id == options->positionals.front();
                                   });
      if (it == records->end()) {
        std::cerr << "record " << options->positionals.front() << " not found\n";
        return 1;
      }
      if (options->json_output) {
        std::cout << json::serialize(RecordToJsonValue(*it)) << '\n';
      } else {
        PrintRecordHuman(*it);
      }
      return 0;
    }
  }

  if (command == "host" && argc >= 3) {
    const std::string subcommand = argv[2];
    if (subcommand == "set-hub") {
      if (argc != 5) {
        PrintUsage();
        return 1;
      }
      vibe::store::FileHostConfigStore host_config_store{vibe::net::DefaultStorageRoot()};
      auto host_identity = vibe::store::EnsureHostIdentity(host_config_store);
      if (!host_identity.has_value()) {
        std::cerr << "failed to initialize host identity\n";
        return 1;
      }
      host_identity->hub_url = std::string(argv[3]);
      host_identity->hub_token = std::string(argv[4]);
      if (!host_config_store.SaveHostIdentity(*host_identity)) {
        std::cerr << "failed to save hub configuration\n";
        return 1;
      }
      std::cout << "hub configuration saved; restart sentrits serve to apply it\n";
      return 0;
    }

    if (subcommand == "clear-hub") {
      vibe::store::FileHostConfigStore host_config_store{vibe::net::DefaultStorageRoot()};
      auto host_identity = vibe::store::EnsureHostIdentity(host_config_store);
      if (!host_identity.has_value()) {
        std::cerr << "failed to initialize host identity\n";
        return 1;
      }
      host_identity->hub_url = std::nullopt;
      host_identity->hub_token = std::nullopt;
      if (!host_config_store.SaveHostIdentity(*host_identity)) {
        std::cerr << "failed to clear hub configuration\n";
        return 1;
      }
      std::cout << "hub configuration cleared\n";
      return 0;
    }

    if (subcommand == "status") {
      const auto options = ParseCommandOptions(argc, argv, 3, LoadConfiguredAdminEndpoint());
      if (!options.has_value()) {
        PrintUsage();
        return 1;
      }
      const auto host_info = vibe::cli::GetHostInfo(options->endpoint);
      if (!host_info.has_value()) {
        std::cerr << "failed to fetch host status via daemon at " << options->endpoint.host << ":"
                  << options->endpoint.port << '\n';
        return 1;
      }
      if (options->json_output || !PrintHostStatusHuman(*host_info)) {
        std::cout << PrettyPrintJson(*host_info) << '\n';
      }
      return 0;
    }

    if (subcommand == "set-name") {
      const auto options = ParseCommandOptions(argc, argv, 3, LoadConfiguredAdminEndpoint());
      if (!options.has_value() || options->positionals.empty()) {
        PrintUsage();
        return 1;
      }
      const auto& new_name = options->positionals.front();
      if (new_name.empty()) {
        std::cerr << "display name must not be empty\n";
        return 1;
      }
      // Load current host info to preserve other fields.
      const auto current_info_str = vibe::cli::GetHostInfo(options->endpoint);
      if (!current_info_str.has_value()) {
        std::cerr << "failed to fetch host info via daemon at " << options->endpoint.host << ":"
                  << options->endpoint.port << '\n';
        return 1;
      }
      boost::system::error_code parse_error;
      const auto current = json::parse(*current_info_str, parse_error);
      if (parse_error || !current.is_object()) {
        std::cerr << "failed to parse host info\n";
        return 1;
      }
      const auto& obj = current.as_object();
      json::object payload;
      payload["displayName"] = new_name;
      // Preserve existing listener config and provider commands.
      payload["adminHost"] = obj.contains("adminHost") ? obj.at("adminHost") : json::value(std::string(vibe::store::kDefaultAdminHost));
      payload["adminPort"] = obj.contains("adminPort") ? obj.at("adminPort") : json::value(vibe::store::kDefaultAdminPort);
      payload["remoteHost"] = obj.contains("remoteHost") ? obj.at("remoteHost") : json::value(std::string(vibe::store::kDefaultRemoteHost));
      payload["remotePort"] = obj.contains("remotePort") ? obj.at("remotePort") : json::value(vibe::store::kDefaultRemotePort);
      if (obj.contains("providerCommands")) {
        const auto& cmds = obj.at("providerCommands").as_object();
        json::object provider_commands;
        if (cmds.contains("codex")) { provider_commands["codex"] = cmds.at("codex"); }
        if (cmds.contains("claude")) { provider_commands["claude"] = cmds.at("claude"); }
        payload["providerCommands"] = std::move(provider_commands);
      }
      const auto result = vibe::cli::PostHostConfig(options->endpoint, json::serialize(payload));
      if (!result.has_value()) {
        std::cerr << "failed to update host display name\n";
        return 1;
      }
      std::cout << "display name updated to: " << new_name << '\n';
      return 0;
    }

    if (subcommand == "set-provider-command" || subcommand == "clear-provider-command") {
      const auto options = ParseCommandOptions(argc, argv, 3, LoadConfiguredAdminEndpoint());
      if (!options.has_value() || !options->provider.has_value()) {
        PrintUsage();
        return 1;
      }
      if (subcommand == "set-provider-command" && options->positionals.empty()) {
        PrintUsage();
        return 1;
      }
      // Load current host info.
      const auto current_info_str = vibe::cli::GetHostInfo(options->endpoint);
      if (!current_info_str.has_value()) {
        std::cerr << "failed to fetch host info via daemon at " << options->endpoint.host << ":"
                  << options->endpoint.port << '\n';
        return 1;
      }
      boost::system::error_code parse_error;
      const auto current = json::parse(*current_info_str, parse_error);
      if (parse_error || !current.is_object()) {
        std::cerr << "failed to parse host info\n";
        return 1;
      }
      const auto& obj = current.as_object();
      json::object payload;
      payload["displayName"] = obj.contains("displayName") ? obj.at("displayName") : json::value(std::string(vibe::store::kDefaultDisplayName));
      payload["adminHost"] = obj.contains("adminHost") ? obj.at("adminHost") : json::value(std::string(vibe::store::kDefaultAdminHost));
      payload["adminPort"] = obj.contains("adminPort") ? obj.at("adminPort") : json::value(vibe::store::kDefaultAdminPort);
      payload["remoteHost"] = obj.contains("remoteHost") ? obj.at("remoteHost") : json::value(std::string(vibe::store::kDefaultRemoteHost));
      payload["remotePort"] = obj.contains("remotePort") ? obj.at("remotePort") : json::value(vibe::store::kDefaultRemotePort);
      json::object provider_commands;
      if (obj.contains("providerCommands")) {
        const auto& cmds = obj.at("providerCommands").as_object();
        if (cmds.contains("codex")) { provider_commands["codex"] = cmds.at("codex"); }
        if (cmds.contains("claude")) { provider_commands["claude"] = cmds.at("claude"); }
      }
      const std::string provider_key =
          (*options->provider == vibe::session::ProviderType::Codex) ? "codex" : "claude";
      if (subcommand == "clear-provider-command") {
        provider_commands[provider_key] = json::array{};
      } else {
        json::array command_array;
        for (const auto& token : options->positionals) {
          command_array.emplace_back(token);
        }
        provider_commands[provider_key] = std::move(command_array);
      }
      payload["providerCommands"] = std::move(provider_commands);
      const auto result = vibe::cli::PostHostConfig(options->endpoint, json::serialize(payload));
      if (!result.has_value()) {
        std::cerr << "failed to update provider command\n";
        return 1;
      }
      if (subcommand == "clear-provider-command") {
        std::cout << "provider command cleared for " << provider_key << '\n';
      } else {
        std::cout << "provider command updated for " << provider_key << '\n';
      }
      return 0;
    }
  }

  PrintUsage();
  return 0;
}
