#include <csignal>
#include <filesystem>
#include <iostream>
#include <optional>
#include <pthread.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <atomic>
#include <vector>

#include "vibe/cli/daemon_client.h"
#include "vibe/net/http_server.h"
#include "vibe/net/local_auth.h"
#include "vibe/session/launch_spec.h"
#include "vibe/session/pty_process_factory.h"
#include "vibe/session/session_record.h"
#include "vibe/session/session_runtime.h"
#include "vibe/session/session_types.h"
#include "vibe/store/file_stores.h"

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
               " [--remote-host HOST] [--remote-port PORT]"
               " [--remote-cert PATH] [--remote-key PATH]\n"
            << "  vibe-hostd local-pty [command [args...]]\n"
            << "  vibe-hostd session-start [--host HOST] [--port PORT] [title]\n"
            << "  vibe-hostd list [--host HOST] [--port PORT]\n"
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

}  // namespace

auto main(const int argc, char** argv) -> int {
  if (argc >= 2 && std::string(argv[1]) == "serve") {
    std::string admin_host = std::string(vibe::store::kDefaultAdminHost);
    std::uint16_t admin_port = vibe::store::kDefaultAdminPort;
    std::string remote_host = std::string(vibe::store::kDefaultRemoteHost);
    std::uint16_t remote_port = vibe::store::kDefaultRemotePort;
    std::optional<vibe::net::RemoteTlsFiles> remote_tls_override = std::nullopt;
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
      if (argc - index == 2) {
        remote_host = argv[index];
        remote_port = static_cast<std::uint16_t>(std::stoi(argv[index + 1]));
        break;
      }
      std::cerr << "invalid serve arguments\n";
      PrintUsage();
      return 1;
    }

    if (!admin_host_explicit || !admin_port_explicit || !remote_host_explicit || !remote_port_explicit) {
      const vibe::store::FileHostConfigStore host_config_store{vibe::net::DefaultStorageRoot()};
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
      }
    }

    vibe::net::HttpServer server(admin_host, admin_port, remote_host, remote_port,
                                 remote_tls_override);
    sigset_t signal_set;
    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGINT);
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

  if (argc >= 2 && std::string(argv[1]) == "session-start") {
    auto [endpoint, arg_index] = ParseEndpointArgs(argc, argv, 2, LoadConfiguredAdminEndpoint());
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

  if (argc >= 2 && std::string(argv[1]) == "list") {
    auto [endpoint, arg_index] = ParseEndpointArgs(argc, argv, 2, LoadConfiguredAdminEndpoint());
    if (arg_index != argc) {
      std::cerr << "invalid list arguments\n";
      PrintUsage();
      return 1;
    }

    const auto sessions = vibe::cli::ListSessions(endpoint);
    if (!sessions.has_value()) {
      std::cerr << "failed to list sessions via daemon at " << endpoint.host << ":"
                << endpoint.port << '\n';
      return 1;
    }

    for (const auto& session : *sessions) {
      std::cout << session.session_id << '\t'
                << (session.title.empty() ? "(untitled)" : session.title) << '\t'
                << (session.activity_state.empty() ? session.status : session.activity_state)
                << '\n';
    }
    return 0;
  }

  if (argc >= 3 && std::string(argv[1]) == "session-attach") {
    auto [endpoint, arg_index] = ParseEndpointArgs(argc, argv, 2, LoadConfiguredAdminEndpoint());
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
