#include "vibe/cli/daemon_client.h"

#include <sys/select.h>
#include <unistd.h>

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/http.hpp>
#include <boost/json.hpp>

#include <array>
#include <chrono>
#include <cstring>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace vibe::cli {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace json = boost::json;
using tcp = asio::ip::tcp;

namespace {

constexpr auto kHttpRequestTimeout = std::chrono::seconds(2);

auto ToStringPort(const std::uint16_t port) -> std::string { return std::to_string(port); }

struct ParsedBaseUrl {
  std::string scheme;
  std::string host;
  std::string port;
  std::string path_prefix;
};

auto ParseBaseUrl(const std::string& url) -> std::optional<ParsedBaseUrl> {
  ParsedBaseUrl parsed;
  std::string remainder;

  if (url.starts_with("http://")) {
    parsed.scheme = "http";
    parsed.port = "80";
    remainder = url.substr(7);
  } else if (url.starts_with("https://")) {
    parsed.scheme = "https";
    parsed.port = "443";
    remainder = url.substr(8);
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

auto ParseHttpResponse(const std::string& raw_response) -> std::optional<http::response<http::string_body>> {
  const std::size_t header_end = raw_response.find("\r\n\r\n");
  if (header_end == std::string::npos) {
    return std::nullopt;
  }

  const std::string header_block = raw_response.substr(0, header_end);
  const std::string body = raw_response.substr(header_end + 4);

  std::istringstream stream(header_block);
  std::string status_line;
  if (!std::getline(stream, status_line)) {
    return std::nullopt;
  }
  if (!status_line.empty() && status_line.back() == '\r') {
    status_line.pop_back();
  }

  std::istringstream status_stream(status_line);
  std::string http_version_token;
  unsigned int status_code = 0;
  if (!(status_stream >> http_version_token >> status_code)) {
    return std::nullopt;
  }

  http::response<http::string_body> response;
  response.version(http_version_token == "HTTP/1.0" ? 10 : 11);
  response.result(static_cast<http::status>(status_code));

  std::size_t content_length = body.size();
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      continue;
    }
    const std::size_t separator = line.find(':');
    if (separator == std::string::npos) {
      return std::nullopt;
    }
    const std::string name = line.substr(0, separator);
    std::string value = line.substr(separator + 1);
    if (!value.empty() && value.front() == ' ') {
      value.erase(0, 1);
    }
    response.set(name, value);
    if (name == "Content-Length") {
      try {
        content_length = static_cast<std::size_t>(std::stoull(value));
      } catch (...) {
        return std::nullopt;
      }
    }
  }

  if (body.size() < content_length) {
    return std::nullopt;
  }

  response.body() = body.substr(0, content_length);
  response.prepare_payload();
  return response;
}

auto PerformHttpRequest(const DaemonEndpoint& endpoint, const http::verb method,
                        const std::string& target, const std::string& body = {},
                        const std::optional<std::string>& content_type = std::nullopt)
    -> std::optional<http::response<http::string_body>> {
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

  http::request<http::string_body> request{method, target, 11};
  request.set(http::field::host, endpoint.host);
  if (content_type.has_value()) {
    request.set(http::field::content_type, *content_type);
  }
  request.body() = body;
  request.prepare_payload();

  http::write(socket, request, error_code);
  if (error_code) {
    std::cerr << "http write failed: " << error_code.message() << '\n';
    return std::nullopt;
  }

  std::string response_bytes;
  response_bytes.reserve(8192);
  std::array<char, 4096> read_buffer{};
  const auto deadline = std::chrono::steady_clock::now() + kHttpRequestTimeout;

  while (true) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      std::cerr << "http request timed out after "
                << std::chrono::duration_cast<std::chrono::milliseconds>(kHttpRequestTimeout).count()
                << "ms\n";
      return std::nullopt;
    }

    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(socket.native_handle(), &read_fds);
    timeval timeout{};
    timeout.tv_sec = static_cast<decltype(timeout.tv_sec)>(remaining.count() / 1000);
    timeout.tv_usec = static_cast<decltype(timeout.tv_usec)>((remaining.count() % 1000) * 1000);

    const int select_result =
        select(socket.native_handle() + 1, &read_fds, nullptr, nullptr, &timeout);
    if (select_result == 0) {
      std::cerr << "http request timed out after "
                << std::chrono::duration_cast<std::chrono::milliseconds>(kHttpRequestTimeout).count()
                << "ms\n";
      return std::nullopt;
    }
    if (select_result < 0) {
      std::cerr << "http read failed: " << std::strerror(errno) << '\n';
      return std::nullopt;
    }

    const std::size_t bytes_read = socket.read_some(asio::buffer(read_buffer), error_code);
    if (bytes_read > 0) {
      response_bytes.append(read_buffer.data(), bytes_read);
      if (const auto parsed_response = ParseHttpResponse(response_bytes);
          parsed_response.has_value()) {
        boost::system::error_code shutdown_error;
        socket.shutdown(tcp::socket::shutdown_both, shutdown_error);
        return parsed_response;
      }
    }

    if (error_code) {
      if (error_code == asio::error::eof) {
        break;
      }
      std::cerr << "http read failed: " << error_code.message() << '\n';
      return std::nullopt;
    }
  }

  const auto parsed_response = ParseHttpResponse(response_bytes);
  if (!parsed_response.has_value()) {
    std::cerr << "http parse failed: partial message\n";
    return std::nullopt;
  }

  boost::system::error_code shutdown_error;
  socket.shutdown(tcp::socket::shutdown_both, shutdown_error);
  return parsed_response;
}

auto ExtractErrorMessage(const std::string& body) -> std::string {
  boost::system::error_code error_code;
  const json::value parsed = json::parse(body, error_code);
  if (error_code || !parsed.is_object()) {
    return body;
  }

  const json::object& object = parsed.as_object();
  std::string message;
  if (const auto error = object.if_contains("error"); error != nullptr && error->is_string()) {
    message = json::value_to<std::string>(*error);
  }
  if (const auto detail = object.if_contains("detail"); detail != nullptr && detail->is_string()) {
    const std::string detail_text = json::value_to<std::string>(*detail);
    std::string combined = message.empty() ? detail_text : message + ": " + detail_text;
    if (const auto session_id = object.if_contains("sessionId");
        session_id != nullptr && session_id->is_string()) {
      combined += " (session " + json::value_to<std::string>(*session_id) + ")";
    }
    return combined;
  }
  if (const auto session_id = object.if_contains("sessionId");
      session_id != nullptr && session_id->is_string()) {
    if (message.empty()) {
      return "session " + json::value_to<std::string>(*session_id);
    }
    return message + " (session " + json::value_to<std::string>(*session_id) + ")";
  }
  return message.empty() ? body : message;
}

auto PerformHttpRequestToBaseUrl(const std::string& base_url, const http::verb method,
                                 const std::string& target_suffix, const std::string& body,
                                 const std::optional<std::string>& content_type,
                                 const std::optional<std::string>& bearer_token)
    -> std::optional<http::response<http::string_body>> {
  const auto parsed = ParseBaseUrl(base_url);
  if (!parsed.has_value()) {
    std::cerr << "invalid hub url: " << base_url << '\n';
    return std::nullopt;
  }
  if (parsed->scheme != "http") {
    std::cerr << "https hub URLs are not supported by this relay proof client yet\n";
    return std::nullopt;
  }

  asio::io_context io_context;
  tcp::resolver resolver(io_context);
  tcp::socket socket(io_context);
  boost::system::error_code error_code;
  const auto results = resolver.resolve(parsed->host, parsed->port, error_code);
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

  const std::string target = parsed->path_prefix + target_suffix;
  http::request<http::string_body> request{method, target.empty() ? "/" : target, 11};
  request.set(http::field::host, parsed->host);
  if (content_type.has_value()) {
    request.set(http::field::content_type, *content_type);
  }
  if (bearer_token.has_value()) {
    request.set(http::field::authorization, "Bearer " + *bearer_token);
  }
  request.body() = body;
  request.prepare_payload();

  http::write(socket, request, error_code);
  if (error_code) {
    std::cerr << "http write failed: " << error_code.message() << '\n';
    return std::nullopt;
  }

  std::string response_bytes;
  response_bytes.reserve(8192);
  std::array<char, 4096> read_buffer{};
  const auto deadline = std::chrono::steady_clock::now() + kHttpRequestTimeout;

  while (true) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      std::cerr << "http request timed out after "
                << std::chrono::duration_cast<std::chrono::milliseconds>(kHttpRequestTimeout).count()
                << "ms\n";
      return std::nullopt;
    }

    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(socket.native_handle(), &read_fds);
    timeval timeout{};
    timeout.tv_sec = static_cast<decltype(timeout.tv_sec)>(remaining.count() / 1000);
    timeout.tv_usec = static_cast<decltype(timeout.tv_usec)>((remaining.count() % 1000) * 1000);

    const int select_result =
        select(socket.native_handle() + 1, &read_fds, nullptr, nullptr, &timeout);
    if (select_result == 0) {
      std::cerr << "http request timed out after "
                << std::chrono::duration_cast<std::chrono::milliseconds>(kHttpRequestTimeout).count()
                << "ms\n";
      return std::nullopt;
    }
    if (select_result < 0) {
      std::cerr << "http read failed: " << std::strerror(errno) << '\n';
      return std::nullopt;
    }

    const std::size_t bytes_read = socket.read_some(asio::buffer(read_buffer), error_code);
    if (bytes_read > 0) {
      response_bytes.append(read_buffer.data(), bytes_read);
      if (const auto parsed_response = ParseHttpResponse(response_bytes);
          parsed_response.has_value()) {
        boost::system::error_code shutdown_error;
        socket.shutdown(tcp::socket::shutdown_both, shutdown_error);
        return parsed_response;
      }
    }

    if (error_code) {
      if (error_code == asio::error::eof) {
        break;
      }
      std::cerr << "http read failed: " << error_code.message() << '\n';
      return std::nullopt;
    }
  }

  return ParseHttpResponse(response_bytes);
}

}  // namespace

auto BuildCreateSessionRequestBody(const CreateSessionRequest& request) -> std::string {
  json::object object;
  if (request.provider.has_value()) {
    object["provider"] = std::string(vibe::session::ToString(*request.provider));
  }
  if (request.workspace_root.has_value()) {
    object["workspaceRoot"] = *request.workspace_root;
  }
  if (request.title.has_value()) {
    object["title"] = *request.title;
  }
  if (request.record_id.has_value()) {
    object["recordId"] = *request.record_id;
  }
  if (request.command_argv.has_value()) {
    json::array command;
    for (const auto& token : *request.command_argv) {
      command.push_back(json::value(token));
    }
    object["commandArgv"] = std::move(command);
  }
  if (request.command_shell.has_value()) {
    object["commandShell"] = *request.command_shell;
  }
  if (request.env_mode.has_value()) {
    object["envMode"] = std::string(vibe::session::ToString(*request.env_mode));
  }
  if (!request.environment_overrides.empty()) {
    json::object overrides;
    for (const auto& [key, value] : request.environment_overrides) {
      overrides[key] = value;
    }
    object["environmentOverrides"] = std::move(overrides);
  }
  if (request.env_file_path.has_value()) {
    object["envFilePath"] = *request.env_file_path;
  }
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

auto BuildRelayRequestBody(const std::string& host_id, const std::string& session_id)
    -> std::string {
  json::object object;
  object["host_id"] = host_id;
  object["session_id"] = session_id;
  return json::serialize(object);
}

auto ParseRelayChannelId(const std::string& body) -> std::optional<std::string> {
  boost::system::error_code error_code;
  const json::value parsed = json::parse(body, error_code);
  if (error_code || !parsed.is_object()) {
    return std::nullopt;
  }

  const json::object& object = parsed.as_object();
  const auto channel_id = object.if_contains("channel_id");
  if (channel_id == nullptr || !channel_id->is_string()) {
    return std::nullopt;
  }
  return json::value_to<std::string>(*channel_id);
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
    if (const auto interaction_kind = object.if_contains("interactionKind");
        interaction_kind != nullptr && interaction_kind->is_string()) {
      session.interaction_kind = json::value_to<std::string>(*interaction_kind);
    }
    if (const auto semantic_preview = object.if_contains("semanticPreview");
        semantic_preview != nullptr && semantic_preview->is_string()) {
      session.semantic_preview = json::value_to<std::string>(*semantic_preview);
    }
    sessions.push_back(std::move(session));
  }

  return sessions;
}

auto ParseRecordList(const std::string& body) -> std::vector<ListedRecord> {
  boost::system::error_code error_code;
  const json::value parsed = json::parse(body, error_code);
  if (error_code || !parsed.is_array()) {
    return {};
  }

  std::vector<ListedRecord> records;
  for (const auto& value : parsed.as_array()) {
    if (!value.is_object()) {
      continue;
    }
    const auto& object = value.as_object();
    const auto record_id = object.if_contains("recordId");
    const auto provider = object.if_contains("provider");
    const auto workspace_root = object.if_contains("workspaceRoot");
    const auto title = object.if_contains("title");
    if (record_id == nullptr || provider == nullptr ||
        workspace_root == nullptr || title == nullptr ||
        !record_id->is_string() || !provider->is_string() ||
        !workspace_root->is_string() || !title->is_string()) {
      continue;
    }

    ListedRecord record{
        .record_id = json::value_to<std::string>(*record_id),
        .provider = json::value_to<std::string>(*provider),
        .workspace_root = json::value_to<std::string>(*workspace_root),
        .title = json::value_to<std::string>(*title),
        .launched_at_unix_ms = 0,
        .conversation_id = std::nullopt,
        .group_tags = {},
        .command_argv = std::nullopt,
        .command_shell = std::nullopt,
    };
    if (const auto launched_at = object.if_contains("launchedAtUnixMs");
        launched_at != nullptr && launched_at->is_int64()) {
      record.launched_at_unix_ms = launched_at->as_int64();
    }
    if (const auto conversation_id = object.if_contains("conversationId");
        conversation_id != nullptr && conversation_id->is_string()) {
      record.conversation_id = json::value_to<std::string>(*conversation_id);
    }
    if (const auto group_tags = object.if_contains("groupTags");
        group_tags != nullptr && group_tags->is_array()) {
      for (const auto& tag : group_tags->as_array()) {
        if (tag.is_string()) {
          record.group_tags.push_back(json::value_to<std::string>(tag));
        }
      }
    }
    if (const auto command_argv = object.if_contains("commandArgv");
        command_argv != nullptr && command_argv->is_array()) {
      std::vector<std::string> tokens;
      for (const auto& token : command_argv->as_array()) {
        if (!token.is_string()) {
          tokens.clear();
          break;
        }
        tokens.push_back(json::value_to<std::string>(token));
      }
      if (!tokens.empty()) {
        record.command_argv = std::move(tokens);
      }
    }
    if (const auto command_shell = object.if_contains("commandShell");
        command_shell != nullptr && command_shell->is_string()) {
      record.command_shell = json::value_to<std::string>(*command_shell);
    }
    records.push_back(std::move(record));
  }

  return records;
}

auto CreateSession(const DaemonEndpoint& endpoint, const CreateSessionRequest& request)
    -> std::optional<std::string> {
  return CreateSessionWithDetail(endpoint, request).session_id;
}

auto CreateSessionWithDetail(const DaemonEndpoint& endpoint, const CreateSessionRequest& request)
    -> CreateSessionResult {
  const auto response = PerformHttpRequest(
      endpoint, http::verb::post, "/sessions", BuildCreateSessionRequestBody(request),
      "application/json");
  if (!response.has_value()) {
    return {};
  }

  if (response->result() != http::status::created) {
    return CreateSessionResult{
        .session_id = std::nullopt,
        .error_message = ExtractErrorMessage(response->body()),
    };
  }

  return CreateSessionResult{
      .session_id = ParseCreatedSessionId(response->body()),
      .error_message = {},
  };
}

auto ListSessions(const DaemonEndpoint& endpoint) -> std::optional<std::vector<ListedSession>> {
  const auto response = PerformHttpRequest(endpoint, http::verb::get, "/sessions");
  if (!response.has_value()) {
    return std::nullopt;
  }

  if (response->result() != http::status::ok) {
    return std::nullopt;
  }

  return ParseSessionList(response->body());
}

auto GetSessionSnapshot(const DaemonEndpoint& endpoint, const std::string& session_id)
    -> std::optional<std::string> {
  const auto response =
      PerformHttpRequest(endpoint, http::verb::get, "/sessions/" + session_id + "/snapshot");
  if (!response.has_value() || response->result() != http::status::ok) {
    return std::nullopt;
  }
  return response->body();
}

auto StopSession(const DaemonEndpoint& endpoint, const std::string& session_id) -> std::optional<std::string> {
  const auto response =
      PerformHttpRequest(endpoint, http::verb::post, "/host/sessions/" + session_id + "/stop");
  if (!response.has_value() || response->result() != http::status::ok) {
    return std::nullopt;
  }
  return response->body();
}

auto ClearInactiveSessions(const DaemonEndpoint& endpoint) -> std::optional<std::string> {
  const auto response = PerformHttpRequest(endpoint, http::verb::post, "/host/sessions/clear-inactive");
  if (!response.has_value() || response->result() != http::status::ok) {
    return std::nullopt;
  }
  return response->body();
}

auto GetHostInfo(const DaemonEndpoint& endpoint) -> std::optional<std::string> {
  const auto response = PerformHttpRequest(endpoint, http::verb::get, "/host/info");
  if (!response.has_value() || response->result() != http::status::ok) {
    return std::nullopt;
  }
  return response->body();
}

auto GetSessionEnv(const DaemonEndpoint& endpoint, const std::string& session_id)
    -> std::optional<std::string> {
  const auto response =
      PerformHttpRequest(endpoint, http::verb::get, "/sessions/" + session_id + "/env");
  if (!response.has_value() || response->result() != http::status::ok) {
    return std::nullopt;
  }
  return response->body();
}

auto ListRecords(const DaemonEndpoint& endpoint) -> std::optional<std::vector<ListedRecord>> {
  const auto response = PerformHttpRequest(endpoint, http::verb::get, "/host/records");
  if (!response.has_value() || response->result() != http::status::ok) {
    return std::nullopt;
  }
  return ParseRecordList(response->body());
}

auto PostHostConfig(const DaemonEndpoint& endpoint, const std::string& body)
    -> std::optional<std::string> {
  const auto response =
      PerformHttpRequest(endpoint, http::verb::post, "/host/config", body, "application/json");
  if (!response.has_value() || response->result() != http::status::ok) {
    return std::nullopt;
  }
  return response->body();
}

auto RequestHubRelayChannel(const std::string& hub_url, const std::string& bearer_token,
                            const std::string& host_id, const std::string& session_id)
    -> std::optional<std::string> {
  const auto response =
      PerformHttpRequestToBaseUrl(hub_url, http::verb::post, "/api/v1/relay/request",
                                  BuildRelayRequestBody(host_id, session_id),
                                  "application/json", bearer_token);
  if (!response.has_value()) {
    return std::nullopt;
  }
  if (response->result() != http::status::ok) {
    std::cerr << "relay request failed: " << ExtractErrorMessage(response->body()) << '\n';
    return std::nullopt;
  }
  return ParseRelayChannelId(response->body());
}

}  // namespace vibe::cli
