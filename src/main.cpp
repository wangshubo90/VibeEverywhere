#include <sys/select.h>
#include <unistd.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "vibe/net/http_server.h"
#include "vibe/session/launch_spec.h"
#include "vibe/session/posix_pty_process.h"
#include "vibe/session/session_record.h"
#include "vibe/session/session_runtime.h"
#include "vibe/session/session_types.h"

namespace {

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
  };
}

auto RunLocalPty(const std::vector<std::string>& command) -> int {
  using vibe::session::LaunchSpec;
  using vibe::session::OutputSlice;
  using vibe::session::PosixPtyProcess;
  using vibe::session::ProviderType;
  using vibe::session::SessionRecord;
  using vibe::session::SessionRuntime;
  using vibe::session::SessionStatus;
  using vibe::session::TerminalSize;

  const auto metadata = MakeLocalSessionMetadata();
  const std::vector<std::string> arguments(command.begin() + 1, command.end());
  const LaunchSpec launch_spec{
      .provider = ProviderType::Codex,
      .executable = command.front(),
      .arguments = arguments,
      .environment_overrides = {},
      .working_directory = metadata.workspace_root,
      .terminal_size = TerminalSize{.columns = 120, .rows = 40},
  };

  PosixPtyProcess process;
  SessionRuntime runtime(SessionRecord(metadata), launch_spec, process, 64U * 1024U);
  if (!runtime.Start()) {
    std::cerr << "failed to start local PTY session\n";
    return 1;
  }

  std::uint64_t next_output_sequence = 1;
  bool stdin_open = true;

  while (true) {
    runtime.PollOnce(50);

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
      continue;
    }

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(STDIN_FILENO, &read_fds);

    timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    const int select_result = select(STDIN_FILENO + 1, &read_fds, nullptr, nullptr, &timeout);
    if (select_result <= 0 || !FD_ISSET(STDIN_FILENO, &read_fds)) {
      continue;
    }

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

void PrintUsage() {
  std::cout << "Usage:\n"
            << "  vibe-hostd serve [bind-address] [port]\n"
            << "  vibe-hostd local-pty [command [args...]]\n";
}

}  // namespace

auto main(const int argc, char** argv) -> int {
  if (argc >= 2 && std::string(argv[1]) == "serve") {
    const std::string bind_address = argc >= 3 ? argv[2] : "127.0.0.1";
    const auto raw_port = argc >= 4 ? std::stoi(argv[3]) : 8080;
    const auto port = static_cast<std::uint16_t>(raw_port);
    vibe::net::HttpServer server(bind_address, port);
    server.Run();
    return 0;
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

  PrintUsage();
  return 0;
}
