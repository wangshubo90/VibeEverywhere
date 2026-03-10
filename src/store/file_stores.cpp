#include "vibe/store/file_stores.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <boost/json.hpp>

#include "vibe/session/session_types.h"

namespace vibe::store {

namespace json = boost::json;

namespace {

constexpr char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

auto HostIdentityPath(const std::filesystem::path& storage_root) -> std::filesystem::path {
  return storage_root / "host_identity.json";
}

auto PairingsPath(const std::filesystem::path& storage_root) -> std::filesystem::path {
  return storage_root / "pairings.json";
}

auto SessionsPath(const std::filesystem::path& storage_root) -> std::filesystem::path {
  return storage_root / "sessions.json";
}

auto Base64Encode(std::string_view input) -> std::string {
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

auto Base64Value(char ch) -> std::optional<std::uint8_t> {
  if (ch >= 'A' && ch <= 'Z') {
    return static_cast<std::uint8_t>(ch - 'A');
  }
  if (ch >= 'a' && ch <= 'z') {
    return static_cast<std::uint8_t>(ch - 'a' + 26);
  }
  if (ch >= '0' && ch <= '9') {
    return static_cast<std::uint8_t>(ch - '0' + 52);
  }
  if (ch == '+') {
    return static_cast<std::uint8_t>(62);
  }
  if (ch == '/') {
    return static_cast<std::uint8_t>(63);
  }
  return std::nullopt;
}

auto Base64Decode(const std::string& input) -> std::optional<std::string> {
  if (input.size() % 4U != 0U) {
    return std::nullopt;
  }

  std::string decoded;
  decoded.reserve((input.size() / 4U) * 3U);

  for (std::size_t index = 0; index < input.size(); index += 4U) {
    const char a = input[index];
    const char b = input[index + 1U];
    const char c = input[index + 2U];
    const char d = input[index + 3U];

    const auto value_a = Base64Value(a);
    const auto value_b = Base64Value(b);
    if (!value_a.has_value() || !value_b.has_value()) {
      return std::nullopt;
    }

    decoded.push_back(static_cast<char>((*value_a << 2U) | (*value_b >> 4U)));

    if (c != '=') {
      const auto value_c = Base64Value(c);
      if (!value_c.has_value()) {
        return std::nullopt;
      }
      decoded.push_back(static_cast<char>(((*value_b & 0x0FU) << 4U) | (*value_c >> 2U)));

      if (d != '=') {
        const auto value_d = Base64Value(d);
        if (!value_d.has_value()) {
          return std::nullopt;
        }
        decoded.push_back(static_cast<char>(((*value_c & 0x03U) << 6U) | *value_d));
      } else if (index + 4U != input.size()) {
        return std::nullopt;
      }
    } else if (d != '=' || index + 4U != input.size()) {
      return std::nullopt;
    }
  }

  return decoded;
}

auto ReadFile(const std::filesystem::path& path) -> std::optional<std::string> {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    return std::nullopt;
  }

  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

auto WriteFileAtomically(const std::filesystem::path& path, const std::string& data) -> bool {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) {
    return false;
  }

  const auto temp_path = path.string() + ".tmp";
  {
    std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
      return false;
    }
    output.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!output.good()) {
      return false;
    }
  }

  std::filesystem::rename(temp_path, path, error);
  if (!error) {
    return true;
  }

  error.clear();
  std::filesystem::remove(path, error);
  error.clear();
  std::filesystem::rename(temp_path, path, error);
  if (error) {
    std::filesystem::remove(temp_path, error);
    return false;
  }

  return true;
}

template <typename T, typename Loader>
auto LoadFromJsonFile(const std::filesystem::path& path, T fallback, Loader loader) -> T {
  const auto content = ReadFile(path);
  if (!content.has_value()) {
    return fallback;
  }

  boost::system::error_code error;
  const json::value value = json::parse(*content, error);
  if (error) {
    return fallback;
  }

  return loader(value).value_or(std::move(fallback));
}

template <typename T, typename Loader>
auto LoadOptionalFromJsonFile(const std::filesystem::path& path, Loader loader) -> std::optional<T> {
  const auto content = ReadFile(path);
  if (!content.has_value()) {
    return std::nullopt;
  }

  boost::system::error_code error;
  const json::value value = json::parse(*content, error);
  if (error) {
    return std::nullopt;
  }

  return loader(value);
}

auto ParseDeviceType(const std::string& value) -> std::optional<vibe::auth::DeviceType> {
  if (value == "unknown") {
    return vibe::auth::DeviceType::Unknown;
  }
  if (value == "mobile") {
    return vibe::auth::DeviceType::Mobile;
  }
  if (value == "desktop") {
    return vibe::auth::DeviceType::Desktop;
  }
  if (value == "browser") {
    return vibe::auth::DeviceType::Browser;
  }
  return std::nullopt;
}

auto ToString(vibe::auth::DeviceType device_type) -> std::string_view {
  switch (device_type) {
    case vibe::auth::DeviceType::Unknown:
      return "unknown";
    case vibe::auth::DeviceType::Mobile:
      return "mobile";
    case vibe::auth::DeviceType::Desktop:
      return "desktop";
    case vibe::auth::DeviceType::Browser:
      return "browser";
  }

  return "unknown";
}

auto ParseProviderType(const std::string& value) -> std::optional<vibe::session::ProviderType> {
  if (value == "codex") {
    return vibe::session::ProviderType::Codex;
  }
  if (value == "claude") {
    return vibe::session::ProviderType::Claude;
  }
  return std::nullopt;
}

auto ParseSessionStatus(const std::string& value) -> std::optional<vibe::session::SessionStatus> {
  if (value == "Created") {
    return vibe::session::SessionStatus::Created;
  }
  if (value == "Starting") {
    return vibe::session::SessionStatus::Starting;
  }
  if (value == "Running") {
    return vibe::session::SessionStatus::Running;
  }
  if (value == "AwaitingInput") {
    return vibe::session::SessionStatus::AwaitingInput;
  }
  if (value == "Exited") {
    return vibe::session::SessionStatus::Exited;
  }
  if (value == "Error") {
    return vibe::session::SessionStatus::Error;
  }
  return std::nullopt;
}

template <typename Collection, typename Predicate, typename Value>
void UpsertBy(Collection& collection, Predicate predicate, Value&& value) {
  const auto it = std::ranges::find_if(collection, predicate);
  if (it == collection.end()) {
    collection.push_back(std::forward<Value>(value));
    return;
  }

  *it = std::forward<Value>(value);
}

template <typename Collection, typename Predicate>
auto RemoveBy(Collection& collection, Predicate predicate) -> bool {
  const auto size_before = collection.size();
  collection.erase(std::remove_if(collection.begin(), collection.end(), predicate), collection.end());
  return collection.size() != size_before;
}

auto HostIdentityFromJson(const json::value& value) -> std::optional<HostIdentity> {
  if (!value.is_object()) {
    return std::nullopt;
  }

  const json::object& object = value.as_object();
  const auto* host_id = object.if_contains("hostId");
  const auto* display_name = object.if_contains("displayName");
  const auto* certificate_pem_path = object.if_contains("certificatePemPath");
  const auto* private_key_pem_path = object.if_contains("privateKeyPemPath");
  if (host_id == nullptr || display_name == nullptr || certificate_pem_path == nullptr ||
      private_key_pem_path == nullptr || !host_id->is_string() || !display_name->is_string() ||
      !certificate_pem_path->is_string() || !private_key_pem_path->is_string()) {
    return std::nullopt;
  }

  HostIdentity identity = MakeDefaultHostIdentity();
  identity.host_id = std::string(host_id->as_string());
  identity.display_name = std::string(display_name->as_string());
  identity.certificate_pem_path = std::string(certificate_pem_path->as_string());
  identity.private_key_pem_path = std::string(private_key_pem_path->as_string());
  return identity;
}

auto ParseProviderCommandOverride(const json::value* value)
    -> std::optional<vibe::store::ProviderCommandOverride> {
  if (value == nullptr) {
    return vibe::store::ProviderCommandOverride{};
  }
  if (!value->is_array()) {
    return std::nullopt;
  }

  const json::array& array = value->as_array();
  if (array.empty()) {
    return vibe::store::ProviderCommandOverride{};
  }
  if (!array.front().is_string()) {
    return std::nullopt;
  }

  vibe::store::ProviderCommandOverride result{
      .executable = std::string(array.front().as_string()),
      .args = {},
  };
  if (result.executable.empty()) {
    return std::nullopt;
  }
  for (std::size_t index = 1; index < array.size(); ++index) {
    if (!array[index].is_string()) {
      return std::nullopt;
    }
    result.args.emplace_back(array[index].as_string());
  }

  return result;
}

auto HostIdentityFromJsonWithDefaults(const json::value& value) -> std::optional<HostIdentity> {
  auto parsed_identity = HostIdentityFromJson(value);
  if (!parsed_identity.has_value()) {
    return std::nullopt;
  }

  HostIdentity identity = *parsed_identity;
  const json::object& object = value.as_object();

  if (const auto* parsed_admin_host = object.if_contains("adminHost"); parsed_admin_host != nullptr) {
    if (!parsed_admin_host->is_string()) {
      return std::nullopt;
    }
    identity.admin_host = std::string(parsed_admin_host->as_string());
    if (identity.admin_host.empty()) {
      return std::nullopt;
    }
  }

  if (const auto* parsed_admin_port = object.if_contains("adminPort"); parsed_admin_port != nullptr) {
    if (!parsed_admin_port->is_int64()) {
      return std::nullopt;
    }
    const auto port = parsed_admin_port->as_int64();
    if (port <= 0 || port > std::numeric_limits<std::uint16_t>::max()) {
      return std::nullopt;
    }
    identity.admin_port = static_cast<std::uint16_t>(port);
  }

  if (const auto* parsed_remote_host = object.if_contains("remoteHost"); parsed_remote_host != nullptr) {
    if (!parsed_remote_host->is_string()) {
      return std::nullopt;
    }
    identity.remote_host = std::string(parsed_remote_host->as_string());
    if (identity.remote_host.empty()) {
      return std::nullopt;
    }
  }

  if (const auto* parsed_remote_port = object.if_contains("remotePort"); parsed_remote_port != nullptr) {
    if (!parsed_remote_port->is_int64()) {
      return std::nullopt;
    }
    const auto port = parsed_remote_port->as_int64();
    if (port <= 0 || port > std::numeric_limits<std::uint16_t>::max()) {
      return std::nullopt;
    }
    identity.remote_port = static_cast<std::uint16_t>(port);
  }

  if (const auto* parsed_provider_commands = object.if_contains("providerCommands");
      parsed_provider_commands != nullptr) {
    if (!parsed_provider_commands->is_object()) {
      return std::nullopt;
    }
    const json::object& commands = parsed_provider_commands->as_object();
    const auto codex_command = ParseProviderCommandOverride(commands.if_contains("codex"));
    const auto claude_command = ParseProviderCommandOverride(commands.if_contains("claude"));
    if (!codex_command.has_value() || !claude_command.has_value()) {
      return std::nullopt;
    }
    identity.codex_command = std::move(*codex_command);
    identity.claude_command = std::move(*claude_command);
  }

  return identity;
}

auto ToJsonValue(const HostIdentity& identity) -> json::value {
  json::object object;
  object["hostId"] = identity.host_id;
  object["displayName"] = identity.display_name;
  object["certificatePemPath"] = identity.certificate_pem_path;
  object["privateKeyPemPath"] = identity.private_key_pem_path;
  object["adminHost"] = identity.admin_host;
  object["adminPort"] = identity.admin_port;
  object["remoteHost"] = identity.remote_host;
  object["remotePort"] = identity.remote_port;
  json::object provider_commands;
  auto to_command_json = [](const vibe::store::ProviderCommandOverride& command) -> json::array {
    json::array array;
    if (!command.executable.empty()) {
      array.push_back(json::value(command.executable));
      for (const auto& arg : command.args) {
        array.push_back(json::value(arg));
      }
    }
    return array;
  };
  provider_commands["codex"] = to_command_json(identity.codex_command);
  provider_commands["claude"] = to_command_json(identity.claude_command);
  object["providerCommands"] = std::move(provider_commands);
  return object;
}

auto PairingRequestFromJson(const json::value& value) -> std::optional<vibe::auth::PairingRequest> {
  if (!value.is_object()) {
    return std::nullopt;
  }

  const json::object& object = value.as_object();
  const auto* pairing_id = object.if_contains("pairingId");
  const auto* device_name = object.if_contains("deviceName");
  const auto* device_type = object.if_contains("deviceType");
  const auto* code = object.if_contains("code");
  const auto* requested_at_unix_ms = object.if_contains("requestedAtUnixMs");
  if (pairing_id == nullptr || device_name == nullptr || device_type == nullptr || code == nullptr ||
      requested_at_unix_ms == nullptr || !pairing_id->is_string() || !device_name->is_string() ||
      !device_type->is_string() || !code->is_string() || !requested_at_unix_ms->is_int64()) {
    return std::nullopt;
  }

  const auto parsed_device_type = ParseDeviceType(std::string(device_type->as_string()));
  if (!parsed_device_type.has_value()) {
    return std::nullopt;
  }

  return vibe::auth::PairingRequest{
      .pairing_id = std::string(pairing_id->as_string()),
      .device_name = std::string(device_name->as_string()),
      .device_type = *parsed_device_type,
      .code = std::string(code->as_string()),
      .requested_at_unix_ms = requested_at_unix_ms->as_int64(),
  };
}

auto ToJsonValue(const vibe::auth::PairingRequest& request) -> json::value {
  json::object object;
  object["pairingId"] = request.pairing_id;
  object["deviceName"] = request.device_name;
  object["deviceType"] = std::string(ToString(request.device_type));
  object["code"] = request.code;
  object["requestedAtUnixMs"] = request.requested_at_unix_ms;
  return object;
}

auto PairingRecordFromJson(const json::value& value) -> std::optional<vibe::auth::PairingRecord> {
  if (!value.is_object()) {
    return std::nullopt;
  }

  const json::object& object = value.as_object();
  const auto* device_id = object.if_contains("deviceId");
  const auto* device_name = object.if_contains("deviceName");
  const auto* device_type = object.if_contains("deviceType");
  const auto* bearer_token = object.if_contains("bearerToken");
  const auto* approved_at_unix_ms = object.if_contains("approvedAtUnixMs");
  if (device_id == nullptr || device_name == nullptr || device_type == nullptr || bearer_token == nullptr ||
      approved_at_unix_ms == nullptr || !device_id->is_string() || !device_name->is_string() ||
      !device_type->is_string() || !bearer_token->is_string() || !approved_at_unix_ms->is_int64()) {
    return std::nullopt;
  }

  const auto parsed_device_type = ParseDeviceType(std::string(device_type->as_string()));
  if (!parsed_device_type.has_value()) {
    return std::nullopt;
  }

  return vibe::auth::PairingRecord{
      .device_id = vibe::auth::DeviceId{.value = std::string(device_id->as_string())},
      .device_name = std::string(device_name->as_string()),
      .device_type = *parsed_device_type,
      .bearer_token = std::string(bearer_token->as_string()),
      .approved_at_unix_ms = approved_at_unix_ms->as_int64(),
  };
}

auto ToJsonValue(const vibe::auth::PairingRecord& record) -> json::value {
  json::object object;
  object["deviceId"] = record.device_id.value;
  object["deviceName"] = record.device_name;
  object["deviceType"] = std::string(ToString(record.device_type));
  object["bearerToken"] = record.bearer_token;
  object["approvedAtUnixMs"] = record.approved_at_unix_ms;
  return object;
}

auto PersistedSessionRecordFromJson(const json::value& value) -> std::optional<PersistedSessionRecord> {
  if (!value.is_object()) {
    return std::nullopt;
  }

  const json::object& object = value.as_object();
  const auto* session_id = object.if_contains("sessionId");
  const auto* provider = object.if_contains("provider");
  const auto* workspace_root = object.if_contains("workspaceRoot");
  const auto* title = object.if_contains("title");
  const auto* status = object.if_contains("status");
  const auto* current_sequence = object.if_contains("currentSequence");
  const auto* recent_terminal_tail = object.if_contains("recentTerminalTailBase64");
  if (session_id == nullptr || provider == nullptr || workspace_root == nullptr || title == nullptr ||
      status == nullptr || current_sequence == nullptr || recent_terminal_tail == nullptr ||
      !session_id->is_string() || !provider->is_string() || !workspace_root->is_string() ||
      !title->is_string() || !status->is_string() || !recent_terminal_tail->is_string()) {
    return std::nullopt;
  }

  const auto parsed_provider = ParseProviderType(std::string(provider->as_string()));
  const auto parsed_status = ParseSessionStatus(std::string(status->as_string()));
  const auto decoded_tail = Base64Decode(std::string(recent_terminal_tail->as_string()));
  if (!parsed_provider.has_value() || !parsed_status.has_value() || !decoded_tail.has_value()) {
    return std::nullopt;
  }

  std::uint64_t parsed_sequence = 0;
  if (current_sequence->is_uint64()) {
    parsed_sequence = current_sequence->as_uint64();
  } else if (current_sequence->is_int64() && current_sequence->as_int64() >= 0) {
    parsed_sequence = static_cast<std::uint64_t>(current_sequence->as_int64());
  } else {
    return std::nullopt;
  }

  return PersistedSessionRecord{
      .session_id = std::string(session_id->as_string()),
      .provider = *parsed_provider,
      .workspace_root = std::string(workspace_root->as_string()),
      .title = std::string(title->as_string()),
      .status = *parsed_status,
      .current_sequence = parsed_sequence,
      .recent_terminal_tail = *decoded_tail,
  };
}

auto ToJsonValue(const PersistedSessionRecord& record) -> json::value {
  json::object object;
  object["sessionId"] = record.session_id;
  object["provider"] = std::string(vibe::session::ToString(record.provider));
  object["workspaceRoot"] = record.workspace_root;
  object["title"] = record.title;
  object["status"] = std::string(vibe::session::ToString(record.status));
  object["currentSequence"] = record.current_sequence;
  object["recentTerminalTailBase64"] = Base64Encode(record.recent_terminal_tail);
  return object;
}

struct PairingSnapshot {
  std::vector<vibe::auth::PairingRequest> pending;
  std::vector<vibe::auth::PairingRecord> approved;

  [[nodiscard]] auto operator==(const PairingSnapshot& other) const -> bool = default;
};

auto PairingSnapshotFromJson(const json::value& value) -> std::optional<PairingSnapshot> {
  if (!value.is_object()) {
    return std::nullopt;
  }

  const json::object& object = value.as_object();
  const auto* pending = object.if_contains("pending");
  const auto* approved = object.if_contains("approved");
  if (pending == nullptr || approved == nullptr || !pending->is_array() || !approved->is_array()) {
    return std::nullopt;
  }

  PairingSnapshot snapshot;
  for (const auto& item : pending->as_array()) {
    auto parsed = PairingRequestFromJson(item);
    if (!parsed.has_value()) {
      return std::nullopt;
    }
    snapshot.pending.push_back(std::move(*parsed));
  }

  for (const auto& item : approved->as_array()) {
    auto parsed = PairingRecordFromJson(item);
    if (!parsed.has_value()) {
      return std::nullopt;
    }
    snapshot.approved.push_back(std::move(*parsed));
  }

  return snapshot;
}

auto ToJsonValue(const PairingSnapshot& snapshot) -> json::value {
  json::array pending;
  for (const auto& request : snapshot.pending) {
    pending.push_back(ToJsonValue(request));
  }

  json::array approved;
  for (const auto& record : snapshot.approved) {
    approved.push_back(ToJsonValue(record));
  }

  json::object object;
  object["pending"] = std::move(pending);
  object["approved"] = std::move(approved);
  return object;
}

auto SessionRecordsFromJson(const json::value& value) -> std::optional<std::vector<PersistedSessionRecord>> {
  if (!value.is_array()) {
    return std::nullopt;
  }

  std::vector<PersistedSessionRecord> records;
  for (const auto& item : value.as_array()) {
    auto parsed = PersistedSessionRecordFromJson(item);
    if (!parsed.has_value()) {
      return std::nullopt;
    }
    records.push_back(std::move(*parsed));
  }

  return records;
}

auto ToJsonValue(const std::vector<PersistedSessionRecord>& records) -> json::value {
  json::array array;
  for (const auto& record : records) {
    array.push_back(ToJsonValue(record));
  }
  return array;
}

auto LoadPairingSnapshot(const std::filesystem::path& path) -> PairingSnapshot {
  return LoadFromJsonFile(path, PairingSnapshot{}, PairingSnapshotFromJson);
}

auto SavePairingSnapshot(const std::filesystem::path& path, const PairingSnapshot& snapshot) -> bool {
  return WriteFileAtomically(path, json::serialize(ToJsonValue(snapshot)));
}

}  // namespace

FileHostConfigStore::FileHostConfigStore(std::filesystem::path storage_root)
    : storage_root_(std::move(storage_root)) {}

auto FileHostConfigStore::LoadHostIdentity() const -> std::optional<HostIdentity> {
  return LoadOptionalFromJsonFile<HostIdentity>(HostIdentityPath(storage_root_),
                                                HostIdentityFromJsonWithDefaults);
}

auto FileHostConfigStore::SaveHostIdentity(const HostIdentity& identity) -> bool {
  return WriteFileAtomically(HostIdentityPath(storage_root_), json::serialize(ToJsonValue(identity)));
}

FilePairingStore::FilePairingStore(std::filesystem::path storage_root)
    : storage_root_(std::move(storage_root)) {}

auto FilePairingStore::LoadPendingPairings() const -> std::vector<vibe::auth::PairingRequest> {
  return LoadPairingSnapshot(PairingsPath(storage_root_)).pending;
}

auto FilePairingStore::LoadApprovedPairings() const -> std::vector<vibe::auth::PairingRecord> {
  return LoadPairingSnapshot(PairingsPath(storage_root_)).approved;
}

auto FilePairingStore::UpsertPendingPairing(const vibe::auth::PairingRequest& request) -> bool {
  PairingSnapshot snapshot = LoadPairingSnapshot(PairingsPath(storage_root_));
  UpsertBy(snapshot.pending,
           [&](const vibe::auth::PairingRequest& existing) { return existing.pairing_id == request.pairing_id; },
           request);
  return SavePairingSnapshot(PairingsPath(storage_root_), snapshot);
}

auto FilePairingStore::UpsertApprovedPairing(const vibe::auth::PairingRecord& record) -> bool {
  PairingSnapshot snapshot = LoadPairingSnapshot(PairingsPath(storage_root_));
  UpsertBy(snapshot.approved,
           [&](const vibe::auth::PairingRecord& existing) {
             return existing.device_id.value == record.device_id.value;
           },
           record);
  return SavePairingSnapshot(PairingsPath(storage_root_), snapshot);
}

auto FilePairingStore::RemovePendingPairing(const std::string& pairing_id) -> bool {
  PairingSnapshot snapshot = LoadPairingSnapshot(PairingsPath(storage_root_));
  if (!RemoveBy(snapshot.pending,
                [&](const vibe::auth::PairingRequest& request) { return request.pairing_id == pairing_id; })) {
    return false;
  }
  return SavePairingSnapshot(PairingsPath(storage_root_), snapshot);
}

auto FilePairingStore::RemoveApprovedPairing(const std::string& device_id) -> bool {
  PairingSnapshot snapshot = LoadPairingSnapshot(PairingsPath(storage_root_));
  if (!RemoveBy(snapshot.approved,
                [&](const vibe::auth::PairingRecord& record) { return record.device_id.value == device_id; })) {
    return false;
  }
  return SavePairingSnapshot(PairingsPath(storage_root_), snapshot);
}

FileSessionStore::FileSessionStore(std::filesystem::path storage_root)
    : storage_root_(std::move(storage_root)) {}

auto FileSessionStore::LoadSessions() const -> std::vector<PersistedSessionRecord> {
  return LoadFromJsonFile(SessionsPath(storage_root_), std::vector<PersistedSessionRecord>{},
                          SessionRecordsFromJson);
}

auto FileSessionStore::UpsertSessionRecord(const PersistedSessionRecord& record) -> bool {
  auto records = LoadSessions();
  UpsertBy(records, [&](const PersistedSessionRecord& existing) { return existing.session_id == record.session_id; },
           record);
  return WriteFileAtomically(SessionsPath(storage_root_), json::serialize(ToJsonValue(records)));
}

auto FileSessionStore::RemoveSessionRecord(const std::string& session_id) -> bool {
  auto records = LoadSessions();
  if (!RemoveBy(records, [&](const PersistedSessionRecord& record) { return record.session_id == session_id; })) {
    return false;
  }
  return WriteFileAtomically(SessionsPath(storage_root_), json::serialize(ToJsonValue(records)));
}

}  // namespace vibe::store
