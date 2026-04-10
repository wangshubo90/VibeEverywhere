#include "vibe/net/http_shared.h"

#include <algorithm>
#include <boost/json.hpp>

#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <limits>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "vibe/net/discovery.h"
#include "vibe/net/json.h"
#include "vibe/net/request_parsing.h"
#include "vibe/base/debug_trace.h"

namespace vibe::net {

namespace {

namespace json = boost::json;

#ifndef SENTRITS_DEFAULT_PACKAGED_WEB_ROOT
#define SENTRITS_DEFAULT_PACKAGED_WEB_ROOT ""
#endif

auto MakeJsonResponse(const HttpRequest& request, http::status status, std::string body) -> HttpResponse;
auto MakeTextResponse(const HttpRequest& request, http::status status, std::string_view content_type,
                      std::string body) -> HttpResponse;

auto EnvPath(const char* name) -> std::optional<std::filesystem::path> {
  const char* value = std::getenv(name);
  if (value == nullptr || *value == '\0') {
    return std::nullopt;
  }

  return std::filesystem::path(value);
}

void AppendCandidatePath(std::vector<std::filesystem::path>& candidates,
                         std::vector<std::string>& seen_paths,
                         const std::filesystem::path& candidate) {
  if (candidate.empty()) {
    return;
  }

  const std::string key = candidate.lexically_normal().string();
  if (std::find(seen_paths.begin(), seen_paths.end(), key) != seen_paths.end()) {
    return;
  }

  seen_paths.push_back(key);
  candidates.push_back(candidate);
}

auto ResolvePackagedWebRoot() -> std::optional<std::filesystem::path> {
  if (const auto web_root = EnvPath("SENTRITS_WEB_ROOT"); web_root.has_value()) {
    return web_root;
  }

  constexpr std::string_view compiled_root = SENTRITS_DEFAULT_PACKAGED_WEB_ROOT;
  if (!compiled_root.empty()) {
    return std::filesystem::path(compiled_root);
  }

  return std::nullopt;
}

auto BuildAssetCandidates(const std::optional<std::filesystem::path>& explicit_root,
                          const std::optional<std::filesystem::path>& packaged_subdir,
                          const std::vector<std::filesystem::path>& development_roots,
                          const std::string_view relative_path) -> std::vector<std::filesystem::path> {
  std::vector<std::filesystem::path> candidates;
  std::vector<std::string> seen_paths;

  if (explicit_root.has_value()) {
    AppendCandidatePath(candidates, seen_paths, *explicit_root / relative_path);
  }
  if (packaged_subdir.has_value()) {
    AppendCandidatePath(candidates, seen_paths, *packaged_subdir / relative_path);
  }
  for (const auto& root : development_roots) {
    AppendCandidatePath(candidates, seen_paths, root / relative_path);
  }

  return candidates;
}

auto ParseUnsignedQueryValue(const std::string& target, const std::string_view key)
    -> std::optional<std::uint16_t> {
  const std::string raw_value = ParseQueryValue(target, key);
  if (raw_value.empty()) {
    return std::nullopt;
  }

  try {
    const unsigned long parsed = std::stoul(raw_value);
    if (parsed == 0 || parsed > std::numeric_limits<std::uint16_t>::max()) {
      return std::nullopt;
    }
    return static_cast<std::uint16_t>(parsed);
  } catch (...) {
    return std::nullopt;
  }
}

auto MakeSnapshotResponse(const HttpRequest& request, const vibe::session::SessionSnapshot& snapshot,
                          const std::optional<vibe::session::TerminalViewportSnapshot>& viewport_snapshot)
    -> HttpResponse {
  std::ostringstream trace;
  trace << "session=" << snapshot.metadata.id.value() << " seq=" << snapshot.current_sequence
        << " screen=" << (snapshot.terminal_screen.has_value() ? "yes" : "no")
        << " viewport=" << (viewport_snapshot.has_value() ? "yes" : "no");
  if (snapshot.terminal_screen.has_value()) {
    trace << " screenRev=" << snapshot.terminal_screen->render_revision
          << " screenLines=" << snapshot.terminal_screen->visible_lines.size()
          << " scrollback=" << snapshot.terminal_screen->scrollback_lines.size();
  }
  if (viewport_snapshot.has_value()) {
    trace << " viewId=" << viewport_snapshot->view_id << " cols=" << viewport_snapshot->columns
          << " rows=" << viewport_snapshot->rows << " viewRev=" << viewport_snapshot->render_revision;
  }
  vibe::base::DebugTrace("core.focus", "snapshot.response", trace.str());

  json::object snapshot_json;
  snapshot_json["sessionId"] = snapshot.metadata.id.value();
  snapshot_json["provider"] = std::string(vibe::session::ToString(snapshot.metadata.provider));
  snapshot_json["workspaceRoot"] = snapshot.metadata.workspace_root;
  snapshot_json["title"] = snapshot.metadata.title;
  snapshot_json["status"] = std::string(vibe::session::ToString(snapshot.metadata.status));
  if (snapshot.metadata.conversation_id.has_value()) {
    snapshot_json["conversationId"] = *snapshot.metadata.conversation_id;
  }
  snapshot_json["groupTags"] = json::value_from(snapshot.metadata.group_tags);
  snapshot_json["currentSequence"] = snapshot.current_sequence;
  snapshot_json["recentTerminalTail"] = snapshot.recent_terminal_tail;
  if (snapshot.terminal_screen.has_value()) {
    json::object terminal_screen;
    terminal_screen["ptyCols"] = snapshot.terminal_screen->columns;
    terminal_screen["ptyRows"] = snapshot.terminal_screen->rows;
    terminal_screen["renderRevision"] = snapshot.terminal_screen->render_revision;
    terminal_screen["cursorRow"] = snapshot.terminal_screen->cursor_row;
    terminal_screen["cursorColumn"] = snapshot.terminal_screen->cursor_column;
    terminal_screen["visibleLines"] = json::value_from(snapshot.terminal_screen->visible_lines);
    terminal_screen["scrollbackLines"] = json::value_from(snapshot.terminal_screen->scrollback_lines);
    terminal_screen["bootstrapAnsi"] = snapshot.terminal_screen->bootstrap_ansi;
    snapshot_json["terminalScreen"] = std::move(terminal_screen);
  }
  snapshot_json["recentFileChanges"] = json::value_from(snapshot.recent_file_changes);

  json::object git;
  git["branch"] = snapshot.git_summary.branch;
  git["modifiedCount"] = snapshot.git_summary.modified_count;
  git["stagedCount"] = snapshot.git_summary.staged_count;
  git["untrackedCount"] = snapshot.git_summary.untracked_count;
  git["modifiedFiles"] = json::value_from(snapshot.git_summary.modified_files);
  git["stagedFiles"] = json::value_from(snapshot.git_summary.staged_files);
  git["untrackedFiles"] = json::value_from(snapshot.git_summary.untracked_files);
  snapshot_json["git"] = std::move(git);

  json::object signals;
  if (snapshot.signals.last_output_at_unix_ms.has_value()) {
    signals["lastOutputAtUnixMs"] = *snapshot.signals.last_output_at_unix_ms;
  }
  if (snapshot.signals.last_activity_at_unix_ms.has_value()) {
    signals["lastActivityAtUnixMs"] = *snapshot.signals.last_activity_at_unix_ms;
  }
  if (snapshot.signals.last_file_change_at_unix_ms.has_value()) {
    signals["lastFileChangeAtUnixMs"] = *snapshot.signals.last_file_change_at_unix_ms;
  }
  if (snapshot.signals.last_git_change_at_unix_ms.has_value()) {
    signals["lastGitChangeAtUnixMs"] = *snapshot.signals.last_git_change_at_unix_ms;
  }
  if (snapshot.signals.last_controller_change_at_unix_ms.has_value()) {
    signals["lastControllerChangeAtUnixMs"] = *snapshot.signals.last_controller_change_at_unix_ms;
  }
  if (snapshot.signals.attention_since_unix_ms.has_value()) {
    signals["attentionSinceUnixMs"] = *snapshot.signals.attention_since_unix_ms;
  }
  if (snapshot.signals.pty_columns.has_value()) {
    signals["ptyCols"] = *snapshot.signals.pty_columns;
  }
  if (snapshot.signals.pty_rows.has_value()) {
    signals["ptyRows"] = *snapshot.signals.pty_rows;
  }
  signals["currentSequence"] = snapshot.signals.current_sequence;
  signals["recentFileChangeCount"] = snapshot.signals.recent_file_change_count;
  signals["supervisionState"] = std::string(vibe::session::ToString(snapshot.signals.supervision_state));
  signals["attentionState"] = std::string(vibe::session::ToString(snapshot.signals.attention_state));
  signals["attentionReason"] = std::string(vibe::session::ToString(snapshot.signals.attention_reason));
  signals["gitDirty"] = snapshot.signals.git_dirty;
  signals["gitBranch"] = snapshot.signals.git_branch;
  signals["gitModifiedCount"] = snapshot.signals.git_modified_count;
  signals["gitStagedCount"] = snapshot.signals.git_staged_count;
  signals["gitUntrackedCount"] = snapshot.signals.git_untracked_count;
  snapshot_json["signals"] = std::move(signals);

  if (viewport_snapshot.has_value()) {
    json::object viewport;
    viewport["viewId"] = viewport_snapshot->view_id;
    viewport["cols"] = viewport_snapshot->columns;
    viewport["rows"] = viewport_snapshot->rows;
    viewport["renderRevision"] = viewport_snapshot->render_revision;
    viewport["totalLineCount"] = viewport_snapshot->total_line_count;
    viewport["viewportTopLine"] = viewport_snapshot->viewport_top_line;
    viewport["horizontalOffset"] = viewport_snapshot->horizontal_offset;
    if (viewport_snapshot->cursor_viewport_row.has_value()) {
      viewport["cursorRow"] = *viewport_snapshot->cursor_viewport_row;
    }
    if (viewport_snapshot->cursor_viewport_column.has_value()) {
      viewport["cursorColumn"] = *viewport_snapshot->cursor_viewport_column;
    }
    viewport["visibleLines"] = json::value_from(viewport_snapshot->visible_lines);
    viewport["bootstrapAnsi"] = viewport_snapshot->bootstrap_ansi;
    snapshot_json["terminalViewport"] = std::move(viewport);
  }

  return MakeJsonResponse(request, http::status::ok, json::serialize(snapshot_json));
}

auto ApplyConfiguredProviderOverride(vibe::service::CreateSessionRequest request,
                                     const vibe::store::HostConfigStore* host_config_store)
    -> vibe::service::CreateSessionRequest {
  if (request.command_argv.has_value() || host_config_store == nullptr) {
    return request;
  }

  const auto host_identity = host_config_store->LoadHostIdentity();
  if (!host_identity.has_value()) {
    return request;
  }

  const vibe::store::ProviderCommandOverride* command_override = nullptr;
  switch (request.provider) {
    case vibe::session::ProviderType::Codex:
      command_override = &host_identity->codex_command;
      break;
    case vibe::session::ProviderType::Claude:
      command_override = &host_identity->claude_command;
      break;
  }

  if (command_override == nullptr || command_override->executable.empty()) {
    return request;
  }

  request.command_argv = std::vector<std::string>{command_override->executable};
  request.command_argv->insert(request.command_argv->end(), command_override->args.begin(),
                               command_override->args.end());
  return request;
}

auto ReadFile(const std::filesystem::path& path) -> std::optional<std::string> {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    return std::nullopt;
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

auto LoadHostUiAsset(const std::string_view relative_path) -> std::optional<std::string> {
  const auto cwd = std::filesystem::current_path();
  const auto packaged_web_root = ResolvePackagedWebRoot();
  const auto explicit_root = EnvPath("SENTRITS_HOST_UI_ROOT");
  const std::vector<std::filesystem::path> development_roots = {
      cwd / "frontend" / "dist" / "host-admin" / "browser",
      cwd.parent_path() / "frontend" / "dist" / "host-admin" / "browser",
      cwd.parent_path().parent_path() / "frontend" / "dist" / "host-admin" / "browser",
      cwd / "deprecated" / "web" / "host_ui",
      cwd.parent_path() / "deprecated" / "web" / "host_ui",
      cwd.parent_path().parent_path() / "deprecated" / "web" / "host_ui",
      cwd / "web" / "host_ui",
      cwd.parent_path() / "web" / "host_ui",
      cwd.parent_path().parent_path() / "web" / "host_ui",
  };
  const std::vector<std::filesystem::path> candidates =
      BuildAssetCandidates(explicit_root,
                           packaged_web_root.has_value()
                               ? std::optional<std::filesystem::path>(*packaged_web_root / "host-admin")
                               : std::nullopt,
                           development_roots, relative_path);

  for (const auto& candidate : candidates) {
    if (const auto file = ReadFile(candidate); file.has_value()) {
      return file;
    }
  }

  return std::nullopt;
}

auto LoadRemoteUiAsset(const std::string_view relative_path) -> std::optional<std::string> {
  const auto cwd = std::filesystem::current_path();
  const auto packaged_web_root = ResolvePackagedWebRoot();
  const auto explicit_root = EnvPath("SENTRITS_REMOTE_UI_ROOT");
  const std::vector<std::filesystem::path> development_roots = {
      cwd / "web" / "remote_client",
      cwd.parent_path() / "web" / "remote_client",
      cwd.parent_path().parent_path() / "web" / "remote_client",
  };
  auto candidates = BuildAssetCandidates(
      explicit_root,
      packaged_web_root.has_value()
          ? std::optional<std::filesystem::path>(*packaged_web_root / "remote-client")
          : std::nullopt,
      development_roots, relative_path);

  for (const auto& candidate : candidates) {
    if (const auto file = ReadFile(candidate); file.has_value()) {
      return file;
    }
  }

  return std::nullopt;
}

auto LoadVendorAsset(const std::string_view relative_path) -> std::optional<std::string> {
  const auto cwd = std::filesystem::current_path();
  const auto packaged_web_root = ResolvePackagedWebRoot();
  const auto explicit_root = EnvPath("SENTRITS_VENDOR_ROOT");
  const std::vector<std::filesystem::path> development_roots = {
      cwd / "web" / "vendor",
      cwd.parent_path() / "web" / "vendor",
      cwd.parent_path().parent_path() / "web" / "vendor",
  };
  const std::vector<std::filesystem::path> candidates =
      BuildAssetCandidates(explicit_root,
                           packaged_web_root.has_value()
                               ? std::optional<std::filesystem::path>(*packaged_web_root / "vendor")
                               : std::nullopt,
                           development_roots, relative_path);

  for (const auto& candidate : candidates) {
    if (const auto file = ReadFile(candidate); file.has_value()) {
      return file;
    }
  }

  return std::nullopt;
}

auto StripQueryString(const std::string_view target) -> std::string_view {
  const auto query_index = target.find('?');
  return query_index == std::string_view::npos ? target : target.substr(0, query_index);
}

auto IsUiStaticAssetPath(const std::string_view path) -> bool {
  const auto filename_index = path.find_last_of('/');
  const auto filename = filename_index == std::string_view::npos ? path : path.substr(filename_index + 1);
  return filename.find('.') != std::string_view::npos;
}

auto GuessContentType(const std::string_view path) -> std::string_view {
  const auto extension = std::filesystem::path(path).extension().string();
  if (extension == ".css") {
    return "text/css; charset=utf-8";
  }
  if (extension == ".js" || extension == ".mjs") {
    return "application/javascript; charset=utf-8";
  }
  if (extension == ".html") {
    return "text/html; charset=utf-8";
  }
  if (extension == ".json") {
    return "application/json; charset=utf-8";
  }
  if (extension == ".png") {
    return "image/png";
  }
  if (extension == ".ico") {
    return "image/x-icon";
  }
  if (extension == ".svg") {
    return "image/svg+xml";
  }
  if (extension == ".map") {
    return "application/json; charset=utf-8";
  }
  return "application/octet-stream";
}

auto MakeStaticAssetResponse(const HttpRequest& request, const std::string_view asset_path,
                             const std::optional<std::string>& asset) -> HttpResponse {
  if (!asset.has_value()) {
    return MakeJsonResponse(request, http::status::service_unavailable, "{\"error\":\"ui asset missing\"}");
  }
  return MakeTextResponse(request, http::status::ok, GuessContentType(asset_path), *asset);
}

auto MakeRandomHexToken(const std::size_t bytes) -> std::string {
  static constexpr char kHexDigits[] = "0123456789abcdef";
  thread_local std::random_device random_device;
  thread_local std::mt19937 generator(random_device());
  std::uniform_int_distribution<int> distribution(0, 255);

  std::string token;
  token.reserve(bytes * 2);
  for (std::size_t index = 0; index < bytes; ++index) {
    const auto value = static_cast<unsigned>(distribution(generator));
    token.push_back(kHexDigits[(value >> 4U) & 0x0FU]);
    token.push_back(kHexDigits[value & 0x0FU]);
  }
  return token;
}

auto MakeFileReadErrorResponse(const HttpRequest& request,
                               const vibe::service::SessionFileReadResult& file_result) -> HttpResponse {
  switch (file_result.status) {
    case vibe::service::FileReadStatus::SessionNotFound:
      return MakeJsonResponse(request, http::status::not_found, "{\"error\":\"session not found\"}");
    case vibe::service::FileReadStatus::InvalidPath:
      return MakeJsonResponse(request, http::status::bad_request, "{\"error\":\"invalid file path\"}");
    case vibe::service::FileReadStatus::WorkspaceUnavailable:
      return MakeJsonResponse(request, http::status::bad_request,
                              "{\"error\":\"workspace root unavailable\"}");
    case vibe::service::FileReadStatus::NotFound:
      return MakeJsonResponse(request, http::status::not_found, "{\"error\":\"file not found\"}");
    case vibe::service::FileReadStatus::NotRegularFile:
      return MakeJsonResponse(request, http::status::bad_request,
                              "{\"error\":\"path does not reference a regular file\"}");
    case vibe::service::FileReadStatus::IoError:
      return MakeJsonResponse(request, http::status::internal_server_error,
                              "{\"error\":\"failed to read file\"}");
    case vibe::service::FileReadStatus::Ok:
      break;
  }

  return MakeJsonResponse(request, http::status::internal_server_error,
                          "{\"error\":\"unexpected file read status\"}");
}

auto AttachClientCounts(std::vector<vibe::service::SessionSummary> summaries,
                        const vibe::net::HostAdmin* host_admin)
    -> std::vector<vibe::service::SessionSummary> {
  if (host_admin == nullptr) {
    return summaries;
  }

  const auto clients = host_admin->ListAttachedClients();
  for (auto& summary : summaries) {
    summary.attached_client_count = static_cast<std::size_t>(std::count_if(
        clients.begin(), clients.end(), [&summary](const vibe::net::AttachedClientInfo& client) {
          return client.session_id == summary.id.value();
        }));
  }
  return summaries;
}

auto AttachClientCount(std::optional<vibe::service::SessionSummary> summary,
                       const vibe::net::HostAdmin* host_admin)
    -> std::optional<vibe::service::SessionSummary> {
  if (!summary.has_value() || host_admin == nullptr) {
    return summary;
  }

  const auto clients = host_admin->ListAttachedClients();
  summary->attached_client_count = static_cast<std::size_t>(std::count_if(
      clients.begin(), clients.end(), [&summary](const vibe::net::AttachedClientInfo& client) {
        return client.session_id == summary->id.value();
      }));
  return summary;
}

void ApplyCorsHeaders(HttpResponse& response) {
  response.set(http::field::access_control_allow_origin, "*");
  response.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
  response.set(http::field::access_control_allow_headers, "content-type, authorization");
}

auto ExtractBearerToken(const HttpRequest& request) -> std::string {
  const auto it = request.base().find(http::field::authorization);
  if (it == request.base().end()) {
    return "";
  }

  const std::string header = std::string(it->value());
  constexpr std::string_view prefix = "Bearer ";
  if (!header.starts_with(prefix)) {
    return "";
  }

  return header.substr(prefix.size());
}

auto MakeJsonResponse(const HttpRequest& request, const http::status status, std::string body) -> HttpResponse {
  HttpResponse response;
  response.version(request.version());
  response.keep_alive(false);
  response.result(status);
  response.set(http::field::content_type, "application/json; charset=utf-8");
  ApplyCorsHeaders(response);
  response.body() = std::move(body);
  response.prepare_payload();
  return response;
}

auto MakeTextResponse(const HttpRequest& request, const http::status status, const std::string_view content_type,
                      std::string body) -> HttpResponse {
  HttpResponse response;
  response.version(request.version());
  response.keep_alive(false);
  response.result(status);
  response.set(http::field::content_type, content_type);
  ApplyCorsHeaders(response);
  response.body() = std::move(body);
  response.prepare_payload();
  return response;
}

auto BuildRequestContext(const HttpRequest& request, const HttpRouteContext& context)
    -> vibe::auth::RequestContext {
  return vibe::auth::RequestContext{
      .bearer_token = ExtractBearerToken(request),
      .client_address = context.client_address,
      .target = std::string(request.target()),
      .is_websocket = false,
      .is_local_request = context.is_local_request,
  };
}

auto RequireAuthorization(const HttpRequest& request, const HttpRouteContext& context,
                          const vibe::auth::AuthorizationAction action) -> std::optional<HttpResponse> {
  if (context.listener_role == ListenerRole::AdminLocal && context.is_local_request &&
      (action == vibe::auth::AuthorizationAction::ObserveSessions ||
       action == vibe::auth::AuthorizationAction::ControlSession)) {
    return std::nullopt;
  }

  if (context.authorizer == nullptr) {
    return std::nullopt;
  }

  const auto auth_result = context.authorizer->Authorize(BuildRequestContext(request, context), action);
  if (auth_result.authorized) {
    return std::nullopt;
  }

  const auto status = auth_result.authenticated ? http::status::forbidden : http::status::unauthorized;
  return MakeJsonResponse(
      request, status,
      "{\"error\":\"" + JsonEscape(auth_result.reason.empty() ? "request rejected" : auth_result.reason) + "\"}");
}

auto RequireLocalAuthorization(const HttpRequest& request, const HttpRouteContext& context,
                               const vibe::auth::AuthorizationAction action) -> std::optional<HttpResponse> {
  if (context.listener_role != ListenerRole::AdminLocal) {
    return MakeJsonResponse(request, http::status::not_found, "{\"error\":\"not found\"}");
  }

  if (!context.is_local_request) {
    return MakeJsonResponse(request, http::status::forbidden, "{\"error\":\"local access required\"}");
  }

  return RequireAuthorization(request, context, action);
}

auto HandlePairingRequest(const HttpRequest& request, const HttpRouteContext& context) -> HttpResponse {
  if (context.pairing_service == nullptr) {
    return MakeJsonResponse(request, http::status::service_unavailable,
                            "{\"error\":\"pairing service unavailable\"}");
  }

  const auto parsed_request = ParsePairingRequest(request.body());
  if (!parsed_request.has_value()) {
    return MakeJsonResponse(request, http::status::bad_request,
                            "{\"error\":\"invalid pairing request\"}");
  }

  const auto pairing_request =
      context.pairing_service->StartPairing(parsed_request->device_name, parsed_request->device_type);
  if (!pairing_request.has_value()) {
    return MakeJsonResponse(request, http::status::internal_server_error,
                            "{\"error\":\"failed to start pairing\"}");
  }

  return MakeJsonResponse(request, http::status::created, ToJson(*pairing_request));
}

auto HandlePairingApprove(const HttpRequest& request, const HttpRouteContext& context) -> HttpResponse {
  if (const auto auth_response =
          RequireLocalAuthorization(request, context, vibe::auth::AuthorizationAction::ApprovePairing);
      auth_response.has_value()) {
    return *auth_response;
  }

  if (context.pairing_service == nullptr) {
    return MakeJsonResponse(request, http::status::service_unavailable,
                            "{\"error\":\"pairing service unavailable\"}");
  }

  const auto parsed_request = ParsePairingApprovalRequest(request.body());
  if (!parsed_request.has_value()) {
    return MakeJsonResponse(request, http::status::bad_request,
                            "{\"error\":\"invalid pairing approval request\"}");
  }

  const auto pairing_record =
      context.pairing_service->ApprovePairing(parsed_request->pairing_id, parsed_request->code);
  if (!pairing_record.has_value()) {
    return MakeJsonResponse(request, http::status::bad_request,
                            "{\"error\":\"pairing approval rejected\"}");
  }

  return MakeJsonResponse(request, http::status::ok, ToJson(*pairing_record));
}

auto HandlePairingReject(const HttpRequest& request, const HttpRouteContext& context) -> HttpResponse {
  if (const auto auth_response =
          RequireLocalAuthorization(request, context, vibe::auth::AuthorizationAction::ApprovePairing);
      auth_response.has_value()) {
    return *auth_response;
  }

  if (context.pairing_service == nullptr) {
    return MakeJsonResponse(request, http::status::service_unavailable,
                            "{\"error\":\"pairing service unavailable\"}");
  }

  const auto parsed_request = ParsePairingApprovalRequest(request.body());
  if (!parsed_request.has_value()) {
    return MakeJsonResponse(request, http::status::bad_request,
                            "{\"error\":\"invalid pairing decision request\"}");
  }

  if (!context.pairing_service->RejectPairing(parsed_request->pairing_id)) {
    return MakeJsonResponse(request, http::status::not_found,
                            "{\"error\":\"pairing request not found\"}");
  }

  return MakeJsonResponse(request, http::status::ok, "{\"status\":\"rejected\"}");
}

auto HandlePairingClaim(const HttpRequest& request, const HttpRouteContext& context) -> HttpResponse {
  if (context.pairing_service == nullptr) {
    return MakeJsonResponse(request, http::status::service_unavailable,
                            "{\"error\":\"pairing service unavailable\"}");
  }

  const auto parsed_request = ParsePairingClaimRequest(request.body());
  if (!parsed_request.has_value()) {
    return MakeJsonResponse(request, http::status::bad_request,
                            "{\"error\":\"invalid pairing claim request\"}");
  }

  const auto pairing_record =
      context.pairing_service->ClaimApprovedPairing(parsed_request->pairing_id, parsed_request->code);
  if (!pairing_record.has_value()) {
    const auto claim_status =
        context.pairing_service->GetPairingClaimStatus(parsed_request->pairing_id, parsed_request->code);
    if (claim_status == vibe::auth::PairingClaimStatus::Rejected) {
      return MakeJsonResponse(request, http::status::ok, "{\"status\":\"rejected\"}");
    }
    if (claim_status == vibe::auth::PairingClaimStatus::Expired) {
      return MakeJsonResponse(request, http::status::ok, "{\"status\":\"expired\"}");
    }
    return MakeJsonResponse(request, http::status::accepted,
                            "{\"status\":\"pending\"}");
  }

  return MakeJsonResponse(request, http::status::ok, ToJson(*pairing_record));
}

auto HandleHostConfig(const HttpRequest& request, const HttpRouteContext& context) -> HttpResponse {
  if (const auto auth_response =
          RequireLocalAuthorization(request, context, vibe::auth::AuthorizationAction::ConfigureHost);
      auth_response.has_value()) {
    return *auth_response;
  }

  if (context.host_config_store == nullptr) {
    return MakeJsonResponse(request, http::status::service_unavailable,
                            "{\"error\":\"host config unavailable\"}");
  }

  const auto parsed_request = ParseHostConfigRequest(request.body());
  if (!parsed_request.has_value()) {
    return MakeJsonResponse(request, http::status::bad_request,
                            "{\"error\":\"invalid host config request\"}");
  }

  const auto existing_identity =
      context.host_config_store->LoadHostIdentity().value_or(vibe::store::MakeDefaultHostIdentity());
  auto updated_identity = existing_identity;
  updated_identity.display_name = parsed_request->display_name;
  updated_identity.admin_host = parsed_request->admin_host;
  updated_identity.admin_port = parsed_request->admin_port;
  updated_identity.remote_host = parsed_request->remote_host;
  updated_identity.remote_port = parsed_request->remote_port;
  if (parsed_request->codex_command.has_value()) {
    updated_identity.codex_command = vibe::store::ProviderCommandOverride{
        .executable = parsed_request->codex_command->front(),
        .args = std::vector<std::string>(parsed_request->codex_command->begin() + 1,
                                         parsed_request->codex_command->end()),
    };
  } else {
    updated_identity.codex_command = {};
  }
  if (parsed_request->claude_command.has_value()) {
    updated_identity.claude_command = vibe::store::ProviderCommandOverride{
        .executable = parsed_request->claude_command->front(),
        .args = std::vector<std::string>(parsed_request->claude_command->begin() + 1,
                                         parsed_request->claude_command->end()),
    };
  } else {
    updated_identity.claude_command = {};
  }
  if (!context.host_config_store->SaveHostIdentity(updated_identity)) {
    return MakeJsonResponse(request, http::status::internal_server_error,
                            "{\"error\":\"failed to save host config\"}");
  }

  return MakeJsonResponse(request, http::status::ok,
                          ToJsonHostInfo(context.host_config_store->LoadHostIdentity(),
                                         context.remote_tls_enabled));
}

auto RequireLocalHostAdmin(const HttpRequest& request, const HttpRouteContext& context)
    -> std::optional<HttpResponse> {
  return RequireLocalAuthorization(request, context, vibe::auth::AuthorizationAction::ConfigureHost);
}

auto HandleLocalBrowserToken(const HttpRequest& request, const HttpRouteContext& context) -> HttpResponse {
  if (const auto auth_response = RequireLocalHostAdmin(request, context); auth_response.has_value()) {
    return *auth_response;
  }

  if (context.pairing_store == nullptr) {
    return MakeJsonResponse(request, http::status::service_unavailable,
                            "{\"error\":\"pairing store unavailable\"}");
  }

  constexpr std::string_view local_device_id = "local_browser_host_ui";
  for (const auto& record : context.pairing_store->LoadApprovedPairings()) {
    if (record.device_id.value == local_device_id) {
      return MakeJsonResponse(request, http::status::ok, ToJson(record));
    }
  }

  const vibe::auth::PairingRecord record{
      .device_id = vibe::auth::DeviceId{.value = std::string(local_device_id)},
      .device_name = "Host Browser",
      .device_type = vibe::auth::DeviceType::Browser,
      .bearer_token = MakeRandomHexToken(24),
      .approved_at_unix_ms = 0,
  };
  if (!context.pairing_store->UpsertApprovedPairing(record)) {
    return MakeJsonResponse(request, http::status::internal_server_error,
                            "{\"error\":\"failed to create local browser token\"}");
  }

  return MakeJsonResponse(request, http::status::ok, ToJson(record));
}

auto HandleHostTlsCertificate(const HttpRequest& request, const HttpRouteContext& context) -> HttpResponse {
  if (const auto auth_response = RequireLocalHostAdmin(request, context); auth_response.has_value()) {
    return *auth_response;
  }

  if (context.host_config_store == nullptr) {
    return MakeJsonResponse(request, http::status::service_unavailable,
                            "{\"error\":\"host config unavailable\"}");
  }

  std::string certificate_path = context.remote_tls_certificate_path;
  if (certificate_path.empty()) {
    const auto host_identity = context.host_config_store->LoadHostIdentity();
    if (host_identity.has_value()) {
      certificate_path = host_identity->certificate_pem_path;
    }
  }

  if (certificate_path.empty()) {
    return MakeJsonResponse(request, http::status::not_found,
                            "{\"error\":\"tls certificate not configured\"}");
  }

  const auto certificate = ReadFile(certificate_path);
  if (!certificate.has_value()) {
    return MakeJsonResponse(request, http::status::not_found,
                            "{\"error\":\"tls certificate file not found\"}");
  }

  HttpResponse response;
  response.version(request.version());
  response.keep_alive(false);
  response.result(http::status::ok);
  response.set(http::field::content_type, "application/x-pem-file");
  response.set(http::field::content_disposition, "attachment; filename=\"vibeeverywhere-remote-cert.pem\"");
  ApplyCorsHeaders(response);
  response.body() = *certificate;
  response.prepare_payload();
  return response;
}

auto HandleCreateSessionRequest(const HttpRequest& request, vibe::service::SessionManager& session_manager,
                                const HttpRouteContext& context) -> HttpResponse {
  const auto parsed_request = ParseCreateSessionRequest(request.body());
  if (!parsed_request.has_value()) {
    return MakeJsonResponse(request, http::status::bad_request,
                            "{\"error\":\"invalid create session request\"}");
  }

  const auto created =
      session_manager.CreateSession(ApplyConfiguredProviderOverride(*parsed_request, context.host_config_store));
  if (!created.has_value()) {
    return MakeJsonResponse(request, http::status::internal_server_error,
                            "{\"error\":\"failed to create session\"}");
  }

  return MakeJsonResponse(request, http::status::created, ToJson(*created));
}

auto MakeSessionGroupTagsResponse(const vibe::service::SessionSummary& summary) -> std::string {
  json::object object;
  object["sessionId"] = summary.id.value();
  object["groupTags"] = json::value_from(summary.group_tags);
  return json::serialize(object);
}

}  // namespace

auto HandleRequest(const HttpRequest& request, vibe::service::SessionManager& session_manager,
                   const HttpRouteContext& context) -> HttpResponse {
  const std::string target_string = std::string(request.target());
  const std::string_view target_path = StripQueryString(target_string);

  if (request.method() == http::verb::options) {
    return MakeTextResponse(request, http::status::no_content, "application/json; charset=utf-8", "");
  }

  if (request.method() == http::verb::get && request.target() == "/health") {
    return MakeTextResponse(request, http::status::ok, "text/plain; charset=utf-8", "ok\n");
  }

  if (request.method() == http::verb::get && target_path == "/") {
    const auto asset = context.listener_role == ListenerRole::AdminLocal
                           ? LoadHostUiAsset("index.html")
                           : LoadRemoteUiAsset("index.html");
    if (!asset.has_value()) {
      return MakeJsonResponse(request, http::status::service_unavailable, "{\"error\":\"ui asset missing\"}");
    }
    if (context.listener_role == ListenerRole::AdminLocal) {
      if (const auto auth_response = RequireLocalHostAdmin(request, context); auth_response.has_value()) {
        return *auth_response;
      }
    }
    return MakeTextResponse(request, http::status::ok, "text/html; charset=utf-8", *asset);
  }

  if (request.method() == http::verb::get && target_path == "/ui") {
    if (context.listener_role != ListenerRole::AdminLocal) {
      return MakeJsonResponse(request, http::status::not_found, "{\"error\":\"not found\"}");
    }
    if (const auto auth_response = RequireLocalHostAdmin(request, context); auth_response.has_value()) {
      return *auth_response;
    }
    const auto asset = LoadHostUiAsset("index.html");
    if (!asset.has_value()) {
      return MakeJsonResponse(request, http::status::service_unavailable,
                              "{\"error\":\"host ui asset missing\"}");
    }
    return MakeTextResponse(request, http::status::ok, "text/html; charset=utf-8", *asset);
  }

  if (request.method() == http::verb::get && (target_path == "/client" || target_path == "/remote")) {
    if (context.listener_role != ListenerRole::RemoteClient) {
      return MakeJsonResponse(request, http::status::not_found, "{\"error\":\"not found\"}");
    }
    const auto asset = LoadRemoteUiAsset("index.html");
    if (!asset.has_value()) {
      return MakeJsonResponse(request, http::status::service_unavailable,
                              "{\"error\":\"remote ui asset missing\"}");
    }
    return MakeTextResponse(request, http::status::ok, "text/html; charset=utf-8", *asset);
  }

  if (request.method() == http::verb::get && target_path == "/remote/app.js") {
    if (context.listener_role != ListenerRole::RemoteClient) {
      return MakeJsonResponse(request, http::status::not_found, "{\"error\":\"not found\"}");
    }
    const auto asset = LoadRemoteUiAsset("app.js");
    if (!asset.has_value()) {
      return MakeJsonResponse(request, http::status::service_unavailable,
                              "{\"error\":\"remote ui asset missing\"}");
    }
    return MakeTextResponse(request, http::status::ok, "application/javascript; charset=utf-8", *asset);
  }

  if (request.method() == http::verb::get && target_path == "/remote/app.css") {
    if (context.listener_role != ListenerRole::RemoteClient) {
      return MakeJsonResponse(request, http::status::not_found, "{\"error\":\"not found\"}");
    }
    const auto asset = LoadRemoteUiAsset("app.css");
    if (!asset.has_value()) {
      return MakeJsonResponse(request, http::status::service_unavailable,
                              "{\"error\":\"remote ui asset missing\"}");
    }
    return MakeTextResponse(request, http::status::ok, "text/css; charset=utf-8", *asset);
  }

  if (request.method() == http::verb::get && target_path == "/assets/xterm/xterm.css") {
    const auto asset = LoadVendorAsset("xterm/xterm.css");
    if (!asset.has_value()) {
      return MakeJsonResponse(request, http::status::service_unavailable,
                              "{\"error\":\"vendor asset missing\"}");
    }
    return MakeTextResponse(request, http::status::ok, "text/css; charset=utf-8", *asset);
  }

  if (request.method() == http::verb::get && target_path == "/assets/xterm/xterm.js") {
    const auto asset = LoadVendorAsset("xterm/xterm.js");
    if (!asset.has_value()) {
      return MakeJsonResponse(request, http::status::service_unavailable,
                              "{\"error\":\"vendor asset missing\"}");
    }
    return MakeTextResponse(request, http::status::ok, "application/javascript; charset=utf-8", *asset);
  }

  if (request.method() == http::verb::get && target_path == "/assets/xterm/addon-fit.js") {
    const auto asset = LoadVendorAsset("xterm/addon-fit.js");
    if (!asset.has_value()) {
      return MakeJsonResponse(request, http::status::service_unavailable,
                              "{\"error\":\"vendor asset missing\"}");
    }
    return MakeTextResponse(request, http::status::ok, "application/javascript; charset=utf-8", *asset);
  }

  if (request.method() == http::verb::get && context.listener_role == ListenerRole::RemoteClient &&
      target_path.rfind("/assets/", 0) == 0) {
    const auto relative_path = std::string(target_path.substr(1));
    if (const auto asset = LoadRemoteUiAsset(relative_path); asset.has_value()) {
      return MakeStaticAssetResponse(request, relative_path, asset);
    }
    const auto vendor_relative_path = std::string(target_path.substr(std::string_view("/assets/").size()));
    return MakeStaticAssetResponse(request, relative_path, LoadVendorAsset(vendor_relative_path));
  }

  if (request.method() == http::verb::get && context.listener_role == ListenerRole::AdminLocal &&
      target_path != "/" && target_path != "/ui" && IsUiStaticAssetPath(target_path)) {
    if (const auto auth_response = RequireLocalHostAdmin(request, context); auth_response.has_value()) {
      return *auth_response;
    }
    const std::string asset_relative_path =
        target_path.rfind("/ui/", 0) == 0 ? std::string(target_path.substr(4)) : std::string(target_path.substr(1));
    return MakeStaticAssetResponse(request, asset_relative_path, LoadHostUiAsset(asset_relative_path));
  }

  if (request.method() == http::verb::get &&
      (target_path == "/ui/terminal" || target_string.rfind("/ui/terminal?", 0) == 0)) {
    if (context.listener_role != ListenerRole::AdminLocal) {
      return MakeJsonResponse(request, http::status::not_found, "{\"error\":\"not found\"}");
    }
    if (const auto auth_response = RequireLocalHostAdmin(request, context); auth_response.has_value()) {
      return *auth_response;
    }
    const auto asset = LoadHostUiAsset("terminal.html");
    if (!asset.has_value()) {
      return MakeJsonResponse(request, http::status::service_unavailable,
                              "{\"error\":\"host ui asset missing\"}");
    }
    return MakeTextResponse(request, http::status::ok, "text/html; charset=utf-8", *asset);
  }

  if (request.method() == http::verb::get && target_path == "/ui/app.js") {
    if (context.listener_role != ListenerRole::AdminLocal) {
      return MakeJsonResponse(request, http::status::not_found, "{\"error\":\"not found\"}");
    }
    if (const auto auth_response = RequireLocalHostAdmin(request, context); auth_response.has_value()) {
      return *auth_response;
    }
    const auto asset = LoadHostUiAsset("app.js");
    if (!asset.has_value()) {
      return MakeJsonResponse(request, http::status::service_unavailable,
                              "{\"error\":\"host ui asset missing\"}");
    }
    return MakeTextResponse(request, http::status::ok, "application/javascript; charset=utf-8", *asset);
  }

  if (request.method() == http::verb::get && target_path == "/ui/app.css") {
    if (context.listener_role != ListenerRole::AdminLocal) {
      return MakeJsonResponse(request, http::status::not_found, "{\"error\":\"not found\"}");
    }
    if (const auto auth_response = RequireLocalHostAdmin(request, context); auth_response.has_value()) {
      return *auth_response;
    }
    const auto asset = LoadHostUiAsset("app.css");
    if (!asset.has_value()) {
      return MakeJsonResponse(request, http::status::service_unavailable,
                              "{\"error\":\"host ui asset missing\"}");
    }
    return MakeTextResponse(request, http::status::ok, "text/css; charset=utf-8", *asset);
  }

  if (request.method() == http::verb::get && target_path == "/ui/terminal.js") {
    if (context.listener_role != ListenerRole::AdminLocal) {
      return MakeJsonResponse(request, http::status::not_found, "{\"error\":\"not found\"}");
    }
    if (const auto auth_response = RequireLocalHostAdmin(request, context); auth_response.has_value()) {
      return *auth_response;
    }
    const auto asset = LoadHostUiAsset("terminal.js");
    if (!asset.has_value()) {
      return MakeJsonResponse(request, http::status::service_unavailable,
                              "{\"error\":\"host ui asset missing\"}");
    }
    return MakeTextResponse(request, http::status::ok, "application/javascript; charset=utf-8", *asset);
  }

  if (request.method() == http::verb::post && request.target() == "/pairing/request") {
    return HandlePairingRequest(request, context);
  }

  if (request.method() == http::verb::get && request.target() == "/pairing/pending") {
    if (const auto auth_response =
            RequireLocalAuthorization(request, context, vibe::auth::AuthorizationAction::ApprovePairing);
        auth_response.has_value()) {
      return *auth_response;
    }

    if (context.pairing_service == nullptr) {
      return MakeJsonResponse(request, http::status::service_unavailable,
                              "{\"error\":\"pairing service unavailable\"}");
    }

    return MakeJsonResponse(request, http::status::ok,
                            ToJson(context.pairing_service->ListPendingPairings()));
  }

  if (request.method() == http::verb::post && request.target() == "/pairing/approve") {
    return HandlePairingApprove(request, context);
  }

  if (request.method() == http::verb::post && request.target() == "/pairing/reject") {
    return HandlePairingReject(request, context);
  }

  if (request.method() == http::verb::post && request.target() == "/pairing/claim") {
    return HandlePairingClaim(request, context);
  }

  if (request.method() == http::verb::post && request.target() == "/host/config") {
    return HandleHostConfig(request, context);
  }

  if (request.method() == http::verb::get && request.target() == "/host/tls/certificate") {
    return HandleHostTlsCertificate(request, context);
  }

  if (request.method() == http::verb::get && request.target() == "/host/info") {
    const auto host_identity =
        context.host_config_store != nullptr ? context.host_config_store->LoadHostIdentity() : std::nullopt;
    return MakeJsonResponse(request, http::status::ok,
                            ToJsonHostInfo(host_identity, context.remote_tls_enabled));
  }

  if (request.method() == http::verb::get && request.target() == "/discovery/info") {
    const auto host_identity =
        context.host_config_store != nullptr ? context.host_config_store->LoadHostIdentity() : std::nullopt;
    return MakeJsonResponse(
        request, http::status::ok,
        ToJson(ResolveDiscoveryInfo(host_identity, context.remote_listener_host,
                                    context.remote_listener_port, context.remote_tls_enabled)));
  }

  if (request.method() == http::verb::get && request.target() == "/host/local-token") {
    return HandleLocalBrowserToken(request, context);
  }

  if (request.method() == http::verb::get && request.target() == "/host/sessions") {
    if (const auto auth_response = RequireLocalHostAdmin(request, context); auth_response.has_value()) {
      return *auth_response;
    }
    return MakeJsonResponse(request, http::status::ok,
                            ToJson(AttachClientCounts(session_manager.ListSessions(), context.host_admin)));
  }

  if (request.method() == http::verb::post && request.target() == "/host/sessions") {
    if (const auto auth_response = RequireLocalHostAdmin(request, context); auth_response.has_value()) {
      return *auth_response;
    }
    return HandleCreateSessionRequest(request, session_manager, context);
  }

  if (request.method() == http::verb::get && request.target() == "/host/clients") {
    if (const auto auth_response = RequireLocalHostAdmin(request, context); auth_response.has_value()) {
      return *auth_response;
    }
    if (context.host_admin == nullptr) {
      return MakeJsonResponse(request, http::status::service_unavailable,
                              "{\"error\":\"host admin unavailable\"}");
    }
    return MakeJsonResponse(request, http::status::ok, ToJson(context.host_admin->ListAttachedClients()));
  }

  if (request.method() == http::verb::get && request.target() == "/host/trusted-devices") {
    if (const auto auth_response = RequireLocalHostAdmin(request, context); auth_response.has_value()) {
      return *auth_response;
    }
    if (context.pairing_store == nullptr) {
      return MakeJsonResponse(request, http::status::service_unavailable,
                              "{\"error\":\"pairing store unavailable\"}");
    }
    return MakeJsonResponse(request, http::status::ok, ToJson(context.pairing_store->LoadApprovedPairings()));
  }

  if (request.method() == http::verb::post) {
    if (request.target() == "/host/sessions/clear-inactive") {
      if (const auto auth_response = RequireLocalHostAdmin(request, context); auth_response.has_value()) {
        return *auth_response;
      }

      const auto removed_count = session_manager.ClearInactiveSessions();
      return MakeJsonResponse(request, http::status::ok,
                              "{\"removedCount\":" + std::to_string(removed_count) + "}");
    }

    constexpr std::string_view host_sessions_prefix = "/host/sessions/";
    constexpr std::string_view stop_suffix = "/stop";
    const std::string target = std::string(request.target());
    if (target.rfind(host_sessions_prefix, 0) == 0 && target.ends_with(std::string(stop_suffix))) {
      if (const auto auth_response = RequireLocalHostAdmin(request, context); auth_response.has_value()) {
        return *auth_response;
      }
      const std::string session_id = target.substr(
          host_sessions_prefix.size(), target.size() - host_sessions_prefix.size() - stop_suffix.size());
      if (session_id.empty()) {
        return MakeJsonResponse(request, http::status::bad_request, "{\"error\":\"invalid session id\"}");
      }
      if (!session_manager.StopSession(session_id)) {
        return MakeJsonResponse(request, http::status::not_found, "{\"error\":\"session not found\"}");
      }
      const auto summary = AttachClientCount(session_manager.GetSession(session_id), context.host_admin);
      return MakeJsonResponse(request, http::status::ok,
                              summary.has_value() ? ToJson(*summary) : "{\"status\":\"stopped\"}");
    }

    constexpr std::string_view host_clients_prefix = "/host/clients/";
    constexpr std::string_view disconnect_suffix = "/disconnect";
    if (target.rfind(host_clients_prefix, 0) == 0 && target.ends_with(std::string(disconnect_suffix))) {
      if (const auto auth_response = RequireLocalHostAdmin(request, context); auth_response.has_value()) {
        return *auth_response;
      }
      if (context.host_admin == nullptr) {
        return MakeJsonResponse(request, http::status::service_unavailable,
                                "{\"error\":\"host admin unavailable\"}");
      }
      const std::string client_id = target.substr(host_clients_prefix.size(),
                                                  target.size() - host_clients_prefix.size() -
                                                      disconnect_suffix.size());
      if (client_id.empty()) {
        return MakeJsonResponse(request, http::status::bad_request, "{\"error\":\"invalid client id\"}");
      }
      if (!context.host_admin->DisconnectClient(client_id)) {
        return MakeJsonResponse(request, http::status::not_found, "{\"error\":\"client not found\"}");
      }
      return MakeJsonResponse(request, http::status::ok, "{\"status\":\"disconnected\"}");
    }

    constexpr std::string_view host_trusted_devices_prefix = "/host/trusted-devices/";
    constexpr std::string_view expire_suffix = "/expire";
    constexpr std::string_view remove_suffix = "/remove";
    if (target.rfind(host_trusted_devices_prefix, 0) == 0 &&
        (target.ends_with(std::string(expire_suffix)) || target.ends_with(std::string(remove_suffix)))) {
      if (const auto auth_response = RequireLocalHostAdmin(request, context); auth_response.has_value()) {
        return *auth_response;
      }
      if (context.pairing_store == nullptr) {
        return MakeJsonResponse(request, http::status::service_unavailable,
                                "{\"error\":\"pairing store unavailable\"}");
      }
      const bool is_expire = target.ends_with(std::string(expire_suffix));
      const std::size_t suffix_size = is_expire ? expire_suffix.size() : remove_suffix.size();
      const std::string device_id =
          target.substr(host_trusted_devices_prefix.size(),
                        target.size() - host_trusted_devices_prefix.size() - suffix_size);
      if (device_id.empty()) {
        return MakeJsonResponse(request, http::status::bad_request, "{\"error\":\"invalid device id\"}");
      }
      if (!context.pairing_store->RemoveApprovedPairing(device_id)) {
        return MakeJsonResponse(request, http::status::not_found, "{\"error\":\"device not found\"}");
      }
      return MakeJsonResponse(request, http::status::ok,
                              std::string("{\"status\":\"") +
                                  (is_expire ? "expired" : "removed") + "\"}");
    }
  }

  if (request.method() == http::verb::get && request.target() == "/sessions") {
    if (const auto auth_response =
            RequireAuthorization(request, context, vibe::auth::AuthorizationAction::ObserveSessions);
        auth_response.has_value()) {
      return *auth_response;
    }

    return MakeJsonResponse(request, http::status::ok,
                            ToJson(AttachClientCounts(session_manager.ListSessions(), context.host_admin)));
  }

  if (request.method() == http::verb::post && request.target() == "/sessions") {
    if (const auto auth_response =
            RequireAuthorization(request, context, vibe::auth::AuthorizationAction::ControlSession);
        auth_response.has_value()) {
      return *auth_response;
    }
    return HandleCreateSessionRequest(request, session_manager, context);
  }

  if (request.method() == http::verb::post && request.target() == "/sessions/clear-inactive") {
    if (const auto auth_response =
            RequireAuthorization(request, context, vibe::auth::AuthorizationAction::ControlSession);
        auth_response.has_value()) {
      return *auth_response;
    }

    const auto removed_count = session_manager.ClearInactiveSessions();
    return MakeJsonResponse(request, http::status::ok,
                            "{\"removedCount\":" + std::to_string(removed_count) + "}");
  }

  const std::string target(request.target());
  const std::string request_path = target.substr(0, target.find('?'));
  const std::string sessions_prefix = "/sessions/";
  if (request_path.rfind(sessions_prefix, 0) == 0) {
    const std::string remainder = request_path.substr(sessions_prefix.size());
    const auto snapshot_suffix = std::string("/snapshot");
    const auto file_suffix = std::string("/file");
    const auto groups_suffix = std::string("/groups");
    const auto input_suffix = std::string("/input");
    const auto stop_suffix = std::string("/stop");
    const auto tail_marker = std::string("/tail");

    if (remainder.size() > snapshot_suffix.size() && remainder.ends_with(snapshot_suffix)) {
      if (const auto auth_response =
              RequireAuthorization(request, context, vibe::auth::AuthorizationAction::ObserveSessions);
          auth_response.has_value()) {
        return *auth_response;
      }

      const std::string session_id = remainder.substr(0, remainder.size() - snapshot_suffix.size());
      const auto snapshot = session_manager.GetSnapshot(session_id);
      if (!snapshot.has_value()) {
        return MakeJsonResponse(request, http::status::not_found, "{\"error\":\"session not found\"}");
      }

      std::optional<vibe::session::TerminalViewportSnapshot> viewport_snapshot;
      const std::string view_id = ParseQueryValue(target, "viewId");
      const auto cols = ParseUnsignedQueryValue(target, "cols");
      const auto rows = ParseUnsignedQueryValue(target, "rows");
      if (!view_id.empty() && cols.has_value() && rows.has_value()) {
        {
          std::ostringstream trace;
          trace << "session=" << session_id << " viewId=" << view_id << " cols=" << *cols
                << " rows=" << *rows;
          vibe::base::DebugTrace("core.focus", "snapshot.request", trace.str());
        }
        const bool updated = session_manager.UpdateViewport(
            session_id, view_id,
            vibe::session::TerminalSize{
                .columns = *cols,
                .rows = *rows,
            });
        if (updated) {
          viewport_snapshot = session_manager.GetViewportSnapshot(session_id, view_id);
        }
      }

      return MakeSnapshotResponse(request, *snapshot, viewport_snapshot);
    }

    if (request.method() == http::verb::get && remainder.size() > file_suffix.size() &&
        remainder.ends_with(file_suffix)) {
      if (const auto auth_response =
              RequireAuthorization(request, context, vibe::auth::AuthorizationAction::ObserveSessions);
          auth_response.has_value()) {
        return *auth_response;
      }

      const std::string session_id = remainder.substr(0, remainder.size() - file_suffix.size());
      const std::string raw_path = ParseQueryValue(target, "path");
      if (raw_path.empty()) {
        return MakeJsonResponse(request, http::status::bad_request, "{\"error\":\"missing file path\"}");
      }

      const auto workspace_path = UrlDecode(raw_path);
      if (!workspace_path.has_value()) {
        return MakeJsonResponse(request, http::status::bad_request, "{\"error\":\"invalid file path\"}");
      }

      const auto file_bytes = ParseFileBytes(target);
      if (!file_bytes.has_value()) {
        return MakeJsonResponse(request, http::status::bad_request, "{\"error\":\"invalid byte limit\"}");
      }

      const auto file = session_manager.ReadFile(session_id, *workspace_path, *file_bytes);
      if (file.status != vibe::service::FileReadStatus::Ok) {
        return MakeFileReadErrorResponse(request, file);
      }

      return MakeJsonResponse(request, http::status::ok, ToJson(file));
    }

    const std::size_t tail_pos = remainder.find(tail_marker);
    if (tail_pos != std::string::npos) {
      if (const auto auth_response =
              RequireAuthorization(request, context, vibe::auth::AuthorizationAction::ObserveSessions);
          auth_response.has_value()) {
        return *auth_response;
      }

      const std::string session_id = remainder.substr(0, tail_pos);
      const auto tail_bytes = ParseTailBytes(target);
      if (!tail_bytes.has_value()) {
        return MakeJsonResponse(request, http::status::bad_request, "{\"error\":\"invalid byte limit\"}");
      }

      const auto tail = session_manager.GetTail(session_id, *tail_bytes);
      if (!tail.has_value()) {
        return MakeJsonResponse(request, http::status::not_found, "{\"error\":\"session not found\"}");
      }

      return MakeJsonResponse(request, http::status::ok, ToJson(*tail));
    }

    if (request.method() == http::verb::post && remainder.size() > input_suffix.size() &&
        remainder.ends_with(input_suffix)) {
      if (const auto auth_response =
              RequireAuthorization(request, context, vibe::auth::AuthorizationAction::ControlSession);
          auth_response.has_value()) {
        return *auth_response;
      }

      const std::string session_id = remainder.substr(0, remainder.size() - input_suffix.size());
      const auto input = ParseInputRequest(request.body());
      if (!input.has_value()) {
        return MakeJsonResponse(request, http::status::bad_request, "{\"error\":\"invalid input request\"}");
      }

      if (!session_manager.SendInput(session_id, *input)) {
        return MakeJsonResponse(request, http::status::bad_request, "{\"error\":\"unable to send input\"}");
      }

      return MakeJsonResponse(request, http::status::ok, "{\"status\":\"ok\"}");
    }

    if (request.method() == http::verb::post && remainder.size() > groups_suffix.size() &&
        remainder.ends_with(groups_suffix)) {
      if (const auto auth_response =
              RequireAuthorization(request, context, vibe::auth::AuthorizationAction::ControlSession);
          auth_response.has_value()) {
        return *auth_response;
      }

      const std::string session_id = remainder.substr(0, remainder.size() - groups_suffix.size());
      const auto payload = ParseSessionGroupTagsUpdateRequest(request.body());
      if (!payload.has_value()) {
        return MakeJsonResponse(request, http::status::bad_request,
                                "{\"error\":\"invalid session group tags request\"}");
      }

      const auto updated = session_manager.UpdateSessionGroupTags(session_id, payload->mode, payload->tags);
      if (!updated.has_value()) {
        return MakeJsonResponse(request, http::status::not_found, "{\"error\":\"session not found\"}");
      }

      return MakeJsonResponse(request, http::status::ok, MakeSessionGroupTagsResponse(*updated));
    }

    if (request.method() == http::verb::post && remainder.size() > stop_suffix.size() &&
        remainder.ends_with(stop_suffix)) {
      if (const auto auth_response =
              RequireAuthorization(request, context, vibe::auth::AuthorizationAction::ControlSession);
          auth_response.has_value()) {
        return *auth_response;
      }

      const std::string session_id = remainder.substr(0, remainder.size() - stop_suffix.size());
      if (!session_manager.StopSession(session_id)) {
        return MakeJsonResponse(request, http::status::bad_request, "{\"error\":\"unable to stop session\"}");
      }

      return MakeJsonResponse(request, http::status::ok, "{\"status\":\"stopped\"}");
    }

    if (const auto auth_response =
            RequireAuthorization(request, context, vibe::auth::AuthorizationAction::ObserveSessions);
        auth_response.has_value()) {
      return *auth_response;
    }

    const auto summary = session_manager.GetSession(remainder);
    if (!summary.has_value()) {
      return MakeJsonResponse(request, http::status::not_found, "{\"error\":\"session not found\"}");
    }

    return MakeJsonResponse(request, http::status::ok, ToJson(*summary));
  }

  return MakeJsonResponse(request, http::status::not_found, "{\"error\":\"not found\"}");
}

}  // namespace vibe::net
