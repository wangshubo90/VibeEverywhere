#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "vibe/cli/daemon_client.h"
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
            << "  vibe-hostd serve [--admin-host HOST] [--admin-port PORT]"
               " [--remote-host HOST] [--remote-port PORT]\n"
            << "  vibe-hostd local-pty [command [args...]]\n"
            << "  vibe-hostd session-start [--host HOST] [--port PORT] [title]\n"
            << "  vibe-hostd session-attach [--host HOST] [--port PORT] <session-id>\n";
}

auto ParseEndpointArgs(const int argc, char** argv, int start_index,
                       vibe::cli::DaemonEndpoint default_endpoint)
    -> std::pair<vibe::cli::DaemonEndpoint, int> {
  int index = start_index;
  while (index < argc) {
    const std::string argument = argv[index];
    if (argument == "--host" && index + 1 < argc) {
      default_endpoint.host = argv[index + 1];
      index += 2;
      continue;
    }
    if (argument == "--port" && index + 1 < argc) {
      default_endpoint.port = static_cast<std::uint16_t>(std::stoi(argv[index + 1]));
      index += 2;
      continue;
    }
    break;
  }

  return {default_endpoint, index};
}

}  // namespace

auto main(const int argc, char** argv) -> int {
  if (argc >= 2 && std::string(argv[1]) == "serve") {
    std::string admin_host = "127.0.0.1";
    std::uint16_t admin_port = 18085;
    std::string remote_host = "0.0.0.0";
    std::uint16_t remote_port = 18086;

    int index = 2;
    while (index < argc) {
      const std::string argument = argv[index];
      if (argument == "--admin-host" && index + 1 < argc) {
        admin_host = argv[index + 1];
        index += 2;
        continue;
      }
      if (argument == "--admin-port" && index + 1 < argc) {
        admin_port = static_cast<std::uint16_t>(std::stoi(argv[index + 1]));
        index += 2;
        continue;
      }
      if (argument == "--remote-host" && index + 1 < argc) {
        remote_host = argv[index + 1];
        index += 2;
        continue;
      }
      if (argument == "--remote-port" && index + 1 < argc) {
        remote_port = static_cast<std::uint16_t>(std::stoi(argv[index + 1]));
        index += 2;
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

    vibe::net::HttpServer server(admin_host, admin_port, remote_host, remote_port);
    return server.Run() ? 0 : 1;
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

  if (argc >= 2 && std::string(argv[1]) == "session-start") {
    auto [endpoint, arg_index] = ParseEndpointArgs(argc, argv, 2, vibe::cli::DaemonEndpoint{});
    const std::string title = arg_index < argc ? argv[arg_index] : "host-session";
    const auto session_id = vibe::cli::CreateSession(
        endpoint,
        vibe::session::ProviderType::Codex,
        std::filesystem::current_path().string(),
        title);
    if (!session_id.has_value()) {
      std::cerr << "failed to create session via daemon at " << endpoint.host << ":"
                << endpoint.port << '\n';
      return 1;
    }

    std::cerr << "session " << *session_id << " created\n";
    return vibe::cli::AttachSession(endpoint, *session_id,
                                    vibe::session::ControllerKind::Host);
  }

  if (argc >= 3 && std::string(argv[1]) == "session-attach") {
    auto [endpoint, arg_index] = ParseEndpointArgs(argc, argv, 2, vibe::cli::DaemonEndpoint{});
    if (arg_index >= argc) {
      std::cerr << "session id required\n";
      return 1;
    }
    return vibe::cli::AttachSession(endpoint, argv[arg_index],
                                    vibe::session::ControllerKind::Host);
  }

  PrintUsage();
  return 0;
}
