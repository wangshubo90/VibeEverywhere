#include "vibe/net/json.h"

#include <boost/json.hpp>

namespace vibe::net {

namespace json = boost::json;

namespace {

constexpr char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

auto ToString(const vibe::auth::DeviceType device_type) -> const char* {
  switch (device_type) {
    case vibe::auth::DeviceType::Mobile:
      return "mobile";
    case vibe::auth::DeviceType::Desktop:
      return "desktop";
    case vibe::auth::DeviceType::Browser:
      return "browser";
    case vibe::auth::DeviceType::Unknown:
    default:
      return "unknown";
  }
}

}  // namespace

auto JsonEscape(const std::string_view input) -> std::string {
  return json::serialize(json::value(input)).substr(1, json::serialize(json::value(input)).size() - 2);
}

auto Base64Encode(const std::string_view input) -> std::string {
  std::string encoded;
  encoded.reserve(((input.size() + 2U) / 3U) * 4U);

  std::size_t index = 0;
  while (index + 3U <= input.size()) {
    const auto a = static_cast<unsigned char>(input[index]);
    const auto b = static_cast<unsigned char>(input[index + 1U]);
    const auto c = static_cast<unsigned char>(input[index + 2U]);
    encoded.push_back(kBase64Alphabet[(a >> 2U) & 0x3FU]);
    encoded.push_back(kBase64Alphabet[((a & 0x03U) << 4U) | ((b >> 4U) & 0x0FU)]);
    encoded.push_back(kBase64Alphabet[((b & 0x0FU) << 2U) | ((c >> 6U) & 0x03U)]);
    encoded.push_back(kBase64Alphabet[c & 0x3FU]);
    index += 3U;
  }

  const std::size_t remaining = input.size() - index;
  if (remaining == 1U) {
    const auto a = static_cast<unsigned char>(input[index]);
    encoded.push_back(kBase64Alphabet[(a >> 2U) & 0x3FU]);
    encoded.push_back(kBase64Alphabet[(a & 0x03U) << 4U]);
    encoded.append("==");
  } else if (remaining == 2U) {
    const auto a = static_cast<unsigned char>(input[index]);
    const auto b = static_cast<unsigned char>(input[index + 1U]);
    encoded.push_back(kBase64Alphabet[(a >> 2U) & 0x3FU]);
    encoded.push_back(kBase64Alphabet[((a & 0x03U) << 4U) | ((b >> 4U) & 0x0FU)]);
    encoded.push_back(kBase64Alphabet[(b & 0x0FU) << 2U]);
    encoded.push_back('=');
  }

  return encoded;
}

auto ToJson(const vibe::service::SessionSummary& summary) -> std::string {
  json::object object;
  object["sessionId"] = summary.id.value();
  object["provider"] = std::string(vibe::session::ToString(summary.provider));
  object["workspaceRoot"] = summary.workspace_root;
  object["title"] = summary.title;
  object["status"] = std::string(vibe::session::ToString(summary.status));
  if (summary.controller_client_id.has_value()) {
    object["controllerClientId"] = *summary.controller_client_id;
  }
  object["controllerKind"] = std::string(vibe::session::ToString(summary.controller_kind));
  return json::serialize(object);
}

auto ToJson(const std::vector<vibe::service::SessionSummary>& summaries) -> std::string {
  json::array array;
  for (const auto& summary : summaries) {
    array.emplace_back(json::parse(ToJson(summary)));
  }
  return json::serialize(array);
}

auto ToJson(const vibe::session::SessionSnapshot& snapshot) -> std::string {
  json::object git;
  git["branch"] = snapshot.git_summary.branch;
  git["modifiedFiles"] = json::value_from(snapshot.git_summary.modified_files);
  git["stagedFiles"] = json::value_from(snapshot.git_summary.staged_files);
  git["untrackedFiles"] = json::value_from(snapshot.git_summary.untracked_files);

  json::object object;
  object["sessionId"] = snapshot.metadata.id.value();
  object["provider"] = std::string(vibe::session::ToString(snapshot.metadata.provider));
  object["workspaceRoot"] = snapshot.metadata.workspace_root;
  object["title"] = snapshot.metadata.title;
  object["status"] = std::string(vibe::session::ToString(snapshot.metadata.status));
  object["currentSequence"] = snapshot.current_sequence;
  object["recentTerminalTail"] = snapshot.recent_terminal_tail;
  object["recentFileChanges"] = json::value_from(snapshot.recent_file_changes);
  object["git"] = std::move(git);
  return json::serialize(object);
}

auto ToJson(const vibe::session::OutputSlice& slice) -> std::string {
  json::object object;
  object["seqStart"] = slice.seq_start;
  object["seqEnd"] = slice.seq_end;
  object["dataBase64"] = Base64Encode(slice.data);
  object["dataEncoding"] = "base64";
  return json::serialize(object);
}

auto ToJson(const vibe::auth::PairingRequest& request) -> std::string {
  json::object object;
  object["pairingId"] = request.pairing_id;
  object["deviceName"] = request.device_name;
  object["deviceType"] = ToString(request.device_type);
  object["code"] = request.code;
  object["requestedAtUnixMs"] = request.requested_at_unix_ms;
  object["status"] = "pending";
  return json::serialize(object);
}

auto ToJson(const std::vector<vibe::auth::PairingRequest>& requests) -> std::string {
  json::array array;
  for (const auto& request : requests) {
    array.emplace_back(json::parse(ToJson(request)));
  }
  return json::serialize(array);
}

auto ToJson(const vibe::auth::PairingRecord& record) -> std::string {
  json::object object;
  object["deviceId"] = record.device_id.value;
  object["deviceName"] = record.device_name;
  object["deviceType"] = ToString(record.device_type);
  object["token"] = record.bearer_token;
  object["status"] = "approved";
  object["approvedAtUnixMs"] = record.approved_at_unix_ms;
  return json::serialize(object);
}

auto ToJson(const TerminalOutputEvent& event) -> std::string {
  json::object object;
  object["type"] = "terminal.output";
  object["sessionId"] = event.session_id;
  object["seqStart"] = event.slice.seq_start;
  object["seqEnd"] = event.slice.seq_end;
  object["dataBase64"] = Base64Encode(event.slice.data);
  object["dataEncoding"] = "base64";
  return json::serialize(object);
}

auto ToJson(const SessionUpdatedEvent& event) -> std::string {
  json::object object;
  object["type"] = "session.updated";
  object["sessionId"] = event.summary.id.value();
  object["provider"] = std::string(vibe::session::ToString(event.summary.provider));
  object["workspaceRoot"] = event.summary.workspace_root;
  object["title"] = event.summary.title;
  object["status"] = std::string(vibe::session::ToString(event.summary.status));
  if (event.summary.controller_client_id.has_value()) {
    object["controllerClientId"] = *event.summary.controller_client_id;
  }
  object["controllerKind"] = std::string(vibe::session::ToString(event.summary.controller_kind));
  return json::serialize(object);
}

auto ToJson(const SessionExitedEvent& event) -> std::string {
  json::object object;
  object["type"] = "session.exited";
  object["sessionId"] = event.session_id;
  object["status"] = std::string(vibe::session::ToString(event.status));
  return json::serialize(object);
}

auto ToJson(const ErrorEvent& event) -> std::string {
  json::object object;
  object["type"] = "error";
  if (!event.session_id.empty()) {
    object["sessionId"] = event.session_id;
  }
  object["code"] = event.code;
  object["message"] = event.message;
  return json::serialize(object);
}

auto ToJsonHostInfo() -> std::string {
  return ToJsonHostInfo(std::nullopt, false);
}

auto ToJsonHostInfo(const std::optional<vibe::store::HostIdentity>& host_identity,
                    const bool tls_enabled) -> std::string {
  json::object object;
  object["hostId"] = host_identity.has_value() ? host_identity->host_id : "local-dev-host";
  object["displayName"] =
      host_identity.has_value() ? host_identity->display_name : "VibeEverywhere Dev Host";
  object["version"] = "0.1.0";
  object["capabilities"] = {"sessions", "rest", "websocket"};
  object["pairingMode"] = "approval";
  json::object tls;
  tls["enabled"] = tls_enabled;
  tls["mode"] = tls_enabled ? "self-signed-planned" : "disabled";
  object["tls"] = std::move(tls);
  return json::serialize(object);
}

}  // namespace vibe::net
