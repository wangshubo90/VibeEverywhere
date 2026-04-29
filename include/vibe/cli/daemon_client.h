#ifndef VIBE_CLI_DAEMON_CLIENT_H
#define VIBE_CLI_DAEMON_CLIENT_H

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "vibe/session/env_config.h"
#include "vibe/session/launch_spec.h"
#include "vibe/session/session_types.h"

namespace vibe::cli {

struct DaemonEndpoint {
  std::string host{"127.0.0.1"};
  std::uint16_t port{18085};
};

struct ListedSession {
  std::string session_id;
  std::string title;
  std::string activity_state;
  std::string status;
  std::string interaction_kind;
  std::string semantic_preview;
};

struct ListedRecord {
  std::string record_id;
  std::string provider;
  std::string workspace_root;
  std::string title;
  std::int64_t launched_at_unix_ms{0};
  std::optional<std::string> conversation_id;
  std::vector<std::string> group_tags;
  std::optional<std::vector<std::string>> command_argv;
  std::optional<std::string> command_shell;
};

struct CreateSessionRequest {
  std::optional<vibe::session::ProviderType> provider;
  std::optional<std::string> workspace_root;
  std::optional<std::string> title;
  std::optional<std::string> record_id;
  std::optional<std::vector<std::string>> command_argv;
  std::optional<std::string> command_shell;
  // Environment model fields.
  std::optional<vibe::session::EnvMode> env_mode{std::nullopt};
  std::unordered_map<std::string, std::string> environment_overrides{};
  std::optional<std::string> env_file_path{std::nullopt};
};

struct CreateSessionResult {
  std::optional<std::string> session_id;
  std::string error_message;
};

[[nodiscard]] auto BuildCreateSessionRequestBody(const CreateSessionRequest& request) -> std::string;
[[nodiscard]] auto ParseCreatedSessionId(const std::string& body) -> std::optional<std::string>;
[[nodiscard]] auto BuildRelayRequestBody(const std::string& host_id, const std::string& session_id)
    -> std::string;
[[nodiscard]] auto ParseRelayChannelId(const std::string& body) -> std::optional<std::string>;
[[nodiscard]] auto ParseSessionList(const std::string& body) -> std::vector<ListedSession>;
[[nodiscard]] auto ParseRecordList(const std::string& body) -> std::vector<ListedRecord>;
[[nodiscard]] auto BuildControlRequestCommand(vibe::session::ControllerKind controller_kind)
    -> std::string;
[[nodiscard]] auto BuildReleaseControlCommand() -> std::string;
[[nodiscard]] auto BuildInputCommand(const std::string& data) -> std::string;
[[nodiscard]] auto BuildResizeCommand(vibe::session::TerminalSize terminal_size) -> std::string;

[[nodiscard]] auto CreateSession(const DaemonEndpoint& endpoint, const CreateSessionRequest& request)
    -> std::optional<std::string>;
[[nodiscard]] auto CreateSessionWithDetail(const DaemonEndpoint& endpoint, const CreateSessionRequest& request)
    -> CreateSessionResult;
[[nodiscard]] auto ListSessions(const DaemonEndpoint& endpoint) -> std::optional<std::vector<ListedSession>>;
[[nodiscard]] auto GetSessionSnapshot(const DaemonEndpoint& endpoint, const std::string& session_id)
    -> std::optional<std::string>;
[[nodiscard]] auto StopSession(const DaemonEndpoint& endpoint, const std::string& session_id) -> std::optional<std::string>;
[[nodiscard]] auto ClearInactiveSessions(const DaemonEndpoint& endpoint) -> std::optional<std::string>;
[[nodiscard]] auto GetHostInfo(const DaemonEndpoint& endpoint) -> std::optional<std::string>;
[[nodiscard]] auto GetSessionEnv(const DaemonEndpoint& endpoint, const std::string& session_id)
    -> std::optional<std::string>;
[[nodiscard]] auto ListRecords(const DaemonEndpoint& endpoint) -> std::optional<std::vector<ListedRecord>>;
[[nodiscard]] auto PostHostConfig(const DaemonEndpoint& endpoint, const std::string& body)
    -> std::optional<std::string>;
[[nodiscard]] auto RequestHubRelayChannel(const std::string& hub_url, const std::string& bearer_token,
                                          const std::string& host_id, const std::string& session_id)
    -> std::optional<std::string>;
[[nodiscard]] auto AttachSession(const DaemonEndpoint& endpoint, const std::string& session_id,
                                 vibe::session::ControllerKind controller_kind) -> int;
[[nodiscard]] auto ObserveSession(const DaemonEndpoint& endpoint, const std::string& session_id) -> int;
[[nodiscard]] auto ObserveHubRelaySession(const std::string& hub_url, const std::string& bearer_token,
                                          const std::string& host_id, const std::string& session_id)
    -> int;

}  // namespace vibe::cli

#endif
