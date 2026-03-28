#include <gtest/gtest.h>

#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <optional>
#include <string>
#include <thread>

#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif

#include "vibe/net/http_server.h"

namespace vibe::cli {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
using tcp = asio::ip::tcp;
namespace http = beast::http;

class SessionAttachFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto pid_component = static_cast<std::uint16_t>(::getpid() % 10000);
    const auto time_component = static_cast<std::uint16_t>(
        std::chrono::steady_clock::now().time_since_epoch().count() % 1000);
    remote_port_ = static_cast<std::uint16_t>(20000 + ((pid_component * 2U + time_component * 2U) % 20000));
    admin_port_ = static_cast<std::uint16_t>(remote_port_ + 1U);
    storage_root_ = std::filesystem::temp_directory_path() /
                    ("vibe-attach-test-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(storage_root_);
    std::filesystem::create_directories(storage_root_);
    trace_dir_ = PrepareTraceDirectory();
    server_trace_path_ = trace_dir_ / "server.log";
    pty_trace_path_ = trace_dir_ / "pty.log";
    ws_trace_path_ = trace_dir_ / "ws.log";
    ::setenv("VIBE_SERVER_TRACE_PATH", server_trace_path_.c_str(), 1);
    ::setenv("VIBE_PTY_TRACE_PATH", pty_trace_path_.c_str(), 1);
    ::setenv("VIBE_WS_TRACE_PATH", ws_trace_path_.c_str(), 1);

    started_.store(false);
    server_thread_ = std::thread([this]() {
      vibe::net::HttpServer server("127.0.0.1", admin_port_, "127.0.0.1", remote_port_, storage_root_);
      server_ = &server;
      started_.store(true);
      const bool ran = server.Run();
      static_cast<void>(ran);
      server_ = nullptr;
    });

    while (!started_.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(WaitForServerReady(std::chrono::milliseconds(3000)));
  }

  void TearDown() override {
    for (const auto& session_id : created_session_ids_) {
      try {
        static_cast<void>(SendRequest(http::verb::post, "/sessions/" + session_id + "/stop"));
      } catch (...) {
      }
    }

    if (child_pid_ > 0) {
      ::kill(child_pid_, SIGTERM);
      int status = 0;
      bool exited = false;
      for (int attempt = 0; attempt < 20; ++attempt) {
        const pid_t wait_result = ::waitpid(child_pid_, &status, WNOHANG);
        if (wait_result == child_pid_) {
          exited = true;
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
      if (!exited) {
        ::kill(child_pid_, SIGKILL);
        ::waitpid(child_pid_, &status, 0);
      }
    }

    if (server_ != nullptr) {
      server_->Stop();
    }
    if (server_thread_.joinable()) {
      server_thread_.join();
    }
    if (master_fd_ >= 0) {
      ::close(master_fd_);
    }
    if (!preserve_trace_dir_ && !trace_path_.empty()) {
      std::filesystem::remove(trace_path_);
    }
    if (!preserve_trace_dir_ && !server_trace_path_.empty()) {
      std::filesystem::remove(server_trace_path_);
    }
    ::unsetenv("VIBE_SERVER_TRACE_PATH");
    if (!preserve_trace_dir_ && !pty_trace_path_.empty()) {
      std::filesystem::remove(pty_trace_path_);
    }
    ::unsetenv("VIBE_PTY_TRACE_PATH");
    if (!preserve_trace_dir_ && !ws_trace_path_.empty()) {
      std::filesystem::remove(ws_trace_path_);
    }
    ::unsetenv("VIBE_WS_TRACE_PATH");
    if (!preserve_trace_dir_ && !trace_dir_.empty()) {
      std::filesystem::remove_all(trace_dir_);
    }
    std::filesystem::remove_all(storage_root_);
  }

  auto PrepareTraceDirectory() -> std::filesystem::path {
    if (const char* configured_dir = std::getenv("VIBE_TRACE_DIR");
        configured_dir != nullptr && *configured_dir != '\0') {
      preserve_trace_dir_ = true;
      const std::filesystem::path trace_dir(configured_dir);
      std::filesystem::remove_all(trace_dir);
      std::filesystem::create_directories(trace_dir);
      return trace_dir;
    }

    const auto timestamp = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto trace_dir =
        std::filesystem::temp_directory_path() / ("vibe-attach-trace-run-" + timestamp);
    std::filesystem::create_directories(trace_dir);
    return trace_dir;
  }

  auto WaitForServerReady(const std::chrono::milliseconds timeout) -> bool {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      try {
        auto response = SendRequest(http::verb::get, "/sessions");
        if (response.result() == http::status::ok) {
          return true;
        }
      } catch (...) {
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }

    return false;
  }

  auto SendRequest(const http::verb method, const std::string& target,
                   const std::string& body = std::string()) -> http::response<http::string_body> {
    asio::io_context io_context;
    tcp::resolver resolver(io_context);
    tcp::socket socket(io_context);
    const auto results = resolver.resolve("127.0.0.1", std::to_string(admin_port_));
    asio::connect(socket, results);

    http::request<http::string_body> request{method, target, 11};
    request.set(http::field::host, "127.0.0.1");
    if (!body.empty()) {
      request.set(http::field::content_type, "application/json");
      request.body() = body;
      request.prepare_payload();
    }

    http::write(socket, request);
    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(socket, buffer, response);
    return response;
  }

  auto CreateSession(const std::string& command_json) -> std::string {
    const std::string body =
        "{\"provider\":\"codex\",\"workspaceRoot\":\".\",\"title\":\"attach-echo\",\"command\":" +
        command_json + "}";
    auto response = SendRequest(http::verb::post, "/sessions", body);
    EXPECT_EQ(response.result(), http::status::created);
    const std::string marker = "\"sessionId\":\"";
    const auto start = response.body().find(marker);
    EXPECT_NE(start, std::string::npos);
    if (start == std::string::npos) {
      return {};
    }
    const auto value_start = start + marker.size();
    const auto value_end = response.body().find('"', value_start);
    EXPECT_NE(value_end, std::string::npos);
    if (value_end == std::string::npos) {
      return {};
    }
    std::string session_id = response.body().substr(value_start, value_end - value_start);
    created_session_ids_.push_back(session_id);
    return session_id;
  }

  void StartAttachClient(const std::string& session_id) {
    ASSERT_LT(master_fd_, 0);
    trace_path_ = trace_dir_ / "attach.log";
    child_pid_ = ::forkpty(&master_fd_, nullptr, nullptr, nullptr);
    ASSERT_GE(child_pid_, 0);
    if (child_pid_ == 0) {
      ::setenv("VIBE_ATTACH_TRACE_PATH", trace_path_.c_str(), 1);
      const std::string port = std::to_string(admin_port_);
      execl(VIBE_HOSTD_PATH, VIBE_HOSTD_PATH, "session-attach", "--host", "127.0.0.1", "--port",
            port.c_str(), session_id.c_str(), static_cast<char*>(nullptr));
      _exit(127);
    }
  }

  auto ReadTrace() const -> std::string {
    std::ifstream input(trace_path_);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
  }

  auto ReadServerTrace() const -> std::string {
    std::ifstream input(server_trace_path_);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
  }

  auto ReadPtyTrace() const -> std::string {
    std::ifstream input(pty_trace_path_);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
  }

  auto ReadWsTrace() const -> std::string {
    std::ifstream input(ws_trace_path_);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
  }

  auto WaitForTraceSubstring(const std::filesystem::path& path, const std::string& needle,
                             const std::chrono::milliseconds timeout) const -> bool {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      std::ifstream input(path);
      std::ostringstream contents;
      contents << input.rdbuf();
      if (contents.str().find(needle) != std::string::npos) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
  }

  auto ReadUntilContains(const std::string& needle, const std::chrono::milliseconds timeout) -> std::string {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::string collected;

    while (std::chrono::steady_clock::now() < deadline) {
      fd_set read_fds;
      FD_ZERO(&read_fds);
      FD_SET(master_fd_, &read_fds);
      timeval wait_time{};
      wait_time.tv_sec = 0;
      wait_time.tv_usec = 20000;
      const int ready = ::select(master_fd_ + 1, &read_fds, nullptr, nullptr, &wait_time);
      if (ready <= 0 || !FD_ISSET(master_fd_, &read_fds)) {
        continue;
      }

      char buffer[4096];
      const ssize_t bytes_read = ::read(master_fd_, buffer, sizeof(buffer));
      if (bytes_read <= 0) {
        break;
      }
      collected.append(buffer, static_cast<std::size_t>(bytes_read));
      if (collected.find(needle) != std::string::npos) {
        return collected;
      }
    }

    return collected;
  }

  auto ReadAvailable(const std::chrono::milliseconds timeout) -> std::string {
    std::string collected;
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(master_fd_, &read_fds);
    timeval wait_time{};
    wait_time.tv_sec = static_cast<long>(timeout.count() / 1000);
    wait_time.tv_usec = static_cast<int>((timeout.count() % 1000) * 1000);
    const int ready = ::select(master_fd_ + 1, &read_fds, nullptr, nullptr, &wait_time);
    if (ready <= 0 || !FD_ISSET(master_fd_, &read_fds)) {
      return collected;
    }

    char buffer[4096];
    const ssize_t bytes_read = ::read(master_fd_, buffer, sizeof(buffer));
    if (bytes_read > 0) {
      collected.append(buffer, static_cast<std::size_t>(bytes_read));
    }
    return collected;
  }

  auto ReadUntilQuiet(const std::chrono::milliseconds quiet_period,
                      const std::chrono::milliseconds timeout) -> std::string {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::string collected;
    auto last_data_time = std::chrono::steady_clock::now();

    while (std::chrono::steady_clock::now() < deadline) {
      const std::string chunk = ReadAvailable(std::chrono::milliseconds(20));
      if (!chunk.empty()) {
        collected += chunk;
        last_data_time = std::chrono::steady_clock::now();
        continue;
      }

      if (std::chrono::steady_clock::now() - last_data_time >= quiet_period) {
        break;
      }
    }

    return collected;
  }

  std::atomic<bool> started_{false};
  vibe::net::HttpServer* server_{nullptr};
  std::thread server_thread_;
  std::filesystem::path storage_root_;
  std::uint16_t remote_port_{0};
  std::uint16_t admin_port_{0};
  pid_t child_pid_{-1};
  int master_fd_{-1};
  bool preserve_trace_dir_{false};
  std::vector<std::string> created_session_ids_;
  std::filesystem::path trace_dir_;
  std::filesystem::path trace_path_;
  std::filesystem::path server_trace_path_;
  std::filesystem::path pty_trace_path_;
  std::filesystem::path ws_trace_path_;
};

TEST_F(SessionAttachFixture, SessionAttachTraceHarnessCapturesInputToEchoLifecycle) {
  const std::string session_id = CreateSession(
      R"(["/usr/bin/perl","-e","$|=1; select(undef, undef, undef, 2.0); system(q(stty -echo -icanon min 1 time 0)); print qq(ready\n); while (sysread(STDIN, $c, 1)) { syswrite(STDOUT, $c); }"])");
  ASSERT_FALSE(session_id.empty());

  StartAttachClient(session_id);
  const std::string initial_output = ReadUntilContains("ready", std::chrono::milliseconds(6000));
  ASSERT_NE(initial_output.find("ready"), std::string::npos)
      << "initial output: " << initial_output << "\nclient trace:\n" << ReadTrace()
      << "\nserver trace:\n" << ReadServerTrace() << "\npty trace:\n" << ReadPtyTrace()
      << "\nws trace:\n" << ReadWsTrace();
  ASSERT_EQ(::write(master_fd_, ",", 1), 1);
  const std::string echoed_output = ReadUntilContains(",", std::chrono::milliseconds(2000));
  ASSERT_NE(echoed_output.find(","), std::string::npos)
      << "echo output: " << echoed_output << "\nclient trace:\n" << ReadTrace()
      << "\nserver trace:\n" << ReadServerTrace() << "\npty trace:\n" << ReadPtyTrace()
      << "\nws trace:\n" << ReadWsTrace();

  ASSERT_TRUE(WaitForTraceSubstring(trace_path_, "stdin.read 1", std::chrono::milliseconds(500)));
  ASSERT_TRUE(WaitForTraceSubstring(server_trace_path_, "send_input.begin 1",
                                    std::chrono::milliseconds(500)));
  ASSERT_TRUE(
      WaitForTraceSubstring(pty_trace_path_, "pty.write.begin 1", std::chrono::milliseconds(500)));
  ASSERT_TRUE(
      WaitForTraceSubstring(ws_trace_path_, "ws.write.binary", std::chrono::milliseconds(500)));

  const std::string client_trace = ReadTrace();
  const std::string server_trace = ReadServerTrace();
  const std::string pty_trace = ReadPtyTrace();
  const std::string ws_trace = ReadWsTrace();

  EXPECT_NE(client_trace.find("stdin.read 1"), std::string::npos);
  EXPECT_NE(client_trace.find("ws.write.input 1"), std::string::npos);
  EXPECT_NE(client_trace.find("stdout.write 1"), std::string::npos);
  EXPECT_NE(server_trace.find("send_input.begin 1"), std::string::npos);
  EXPECT_NE(server_trace.find("poll.output 1"), std::string::npos);
  EXPECT_NE(pty_trace.find("pty.write.begin 1"), std::string::npos);
  EXPECT_NE(pty_trace.find("pty.read.data 1"), std::string::npos);
  EXPECT_NE(ws_trace.find("ws.write.binary 1"), std::string::npos);
}

TEST_F(SessionAttachFixture, DISABLED_SessionAttachEchoesFirstTypedByteWithoutWaitingForSecondByte) {
  const std::string session_id = CreateSession(
      R"(["/usr/bin/perl","-e","$|=1; select(undef, undef, undef, 2.0); system(q(stty -echo -icanon min 1 time 0)); print qq(ready\n); while (sysread(STDIN, $c, 1)) { syswrite(STDOUT, $c); }"])");
  ASSERT_FALSE(session_id.empty());

  StartAttachClient(session_id);

  const std::string initial_output = ReadUntilContains("ready", std::chrono::milliseconds(6000));
  ASSERT_NE(initial_output.find("ready"), std::string::npos)
      << "initial output: " << initial_output << "\nclient trace:\n" << ReadTrace()
      << "\nserver trace:\n" << ReadServerTrace() << "\npty trace:\n" << ReadPtyTrace()
      << "\nws trace:\n" << ReadWsTrace();

  ASSERT_EQ(::write(master_fd_, ",", 1), 1);
  const std::string first_echo = ReadUntilContains(",", std::chrono::milliseconds(150));

  EXPECT_NE(first_echo.find(","), std::string::npos)
      << "first key was not rendered promptly; collected output: " << first_echo
      << "\nclient trace:\n" << ReadTrace() << "\nserver trace:\n" << ReadServerTrace()
      << "\npty trace:\n" << ReadPtyTrace() << "\nws trace:\n" << ReadWsTrace();
}

TEST_F(SessionAttachFixture, DISABLED_SessionAttachDoesNotBatchTwoQuickSingleByteEchoes) {
  const std::string session_id = CreateSession(
      R"(["/usr/bin/perl","-e","$|=1; select(undef, undef, undef, 2.0); system(q(stty -echo -icanon min 1 time 0)); print qq(ready\n); while (sysread(STDIN, $c, 1)) { syswrite(STDOUT, $c); }"])");
  ASSERT_FALSE(session_id.empty());

  StartAttachClient(session_id);

  const std::string initial_output = ReadUntilContains("ready", std::chrono::milliseconds(6000));
  ASSERT_NE(initial_output.find("ready"), std::string::npos)
      << "initial output: " << initial_output << "\nclient trace:\n" << ReadTrace()
      << "\nserver trace:\n" << ReadServerTrace() << "\npty trace:\n" << ReadPtyTrace()
      << "\nws trace:\n" << ReadWsTrace();

  ASSERT_EQ(::write(master_fd_, ",", 1), 1);
  const std::string first_window = ReadAvailable(std::chrono::milliseconds(8));
  ASSERT_EQ(::write(master_fd_, " ", 1), 1);
  const std::string combined_output = first_window + ReadUntilContains(", ", std::chrono::milliseconds(200));

  EXPECT_NE(first_window.find(","), std::string::npos)
      << "first key was still buffered when the second key was sent; combined output: " << combined_output
      << "\nclient trace:\n" << ReadTrace() << "\nserver trace:\n" << ReadServerTrace()
      << "\npty trace:\n" << ReadPtyTrace() << "\nws trace:\n" << ReadWsTrace();
  EXPECT_NE(combined_output.find(", "), std::string::npos)
      << "expected both echoed bytes to arrive eventually; combined output: " << combined_output
      << "\nclient trace:\n" << ReadTrace() << "\nserver trace:\n" << ReadServerTrace()
      << "\npty trace:\n" << ReadPtyTrace() << "\nws trace:\n" << ReadWsTrace();
}

TEST_F(SessionAttachFixture, DISABLED_SessionAttachDoesNotBatchTwoQuickReadlineEchoes) {
  const std::string session_id = CreateSession(R"(["/bin/bash","--noprofile","--norc","-i"])");
  ASSERT_FALSE(session_id.empty());

  StartAttachClient(session_id);

  const std::string startup_output =
      ReadUntilQuiet(std::chrono::milliseconds(200), std::chrono::milliseconds(2500));

  ASSERT_EQ(::write(master_fd_, ",", 1), 1);
  const std::string first_window = ReadAvailable(std::chrono::milliseconds(8));
  ASSERT_EQ(::write(master_fd_, " ", 1), 1);
  const std::string combined_output = first_window + ReadUntilContains(", ", std::chrono::milliseconds(250));

  static_cast<void>(::write(master_fd_, "\x03exit\n", 6));
  static_cast<void>(ReadAvailable(std::chrono::milliseconds(100)));

  EXPECT_NE(first_window.find(","), std::string::npos)
      << "interactive shell did not render the first key before the second arrived; startup: " << startup_output
      << " combined output: " << combined_output << "\nclient trace:\n" << ReadTrace()
      << "\nserver trace:\n" << ReadServerTrace() << "\npty trace:\n" << ReadPtyTrace()
      << "\nws trace:\n" << ReadWsTrace();
  EXPECT_NE(combined_output.find(", "), std::string::npos)
      << "interactive shell did not echo both keys; startup: " << startup_output
      << " combined output: " << combined_output << "\nclient trace:\n" << ReadTrace()
      << "\nserver trace:\n" << ReadServerTrace() << "\npty trace:\n" << ReadPtyTrace()
      << "\nws trace:\n" << ReadWsTrace();
}

}  // namespace
}  // namespace vibe::cli
