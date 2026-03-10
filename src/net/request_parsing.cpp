#include "vibe/net/request_parsing.h"

#include <boost/json.hpp>

namespace vibe::net {

namespace json = boost::json;

auto ParseCreateSessionRequest(const std::string& body)
    -> std::optional<vibe::service::CreateSessionRequest> {
  boost::system::error_code error_code;
  const json::value parsed = json::parse(body, error_code);
  if (error_code || !parsed.is_object()) {
    return std::nullopt;
  }

  const json::object& object = parsed.as_object();
  const auto provider_value = object.if_contains("provider");
  const auto workspace_root = object.if_contains("workspaceRoot");
  const auto title = object.if_contains("title");

  if (provider_value == nullptr || workspace_root == nullptr || title == nullptr ||
      !provider_value->is_string() || !workspace_root->is_string() || !title->is_string()) {
    return std::nullopt;
  }

  const std::string provider_name = json::value_to<std::string>(*provider_value);
  vibe::session::ProviderType provider = vibe::session::ProviderType::Codex;
  if (provider_name == "claude") {
    provider = vibe::session::ProviderType::Claude;
  } else if (provider_name != "codex") {
    return std::nullopt;
  }

  return vibe::service::CreateSessionRequest{
      .provider = provider,
      .workspace_root = json::value_to<std::string>(*workspace_root),
      .title = json::value_to<std::string>(*title),
  };
}

auto ParseInputRequest(const std::string& body) -> std::optional<std::string> {
  boost::system::error_code error_code;
  const json::value parsed = json::parse(body, error_code);
  if (error_code || !parsed.is_object()) {
    return std::nullopt;
  }

  const json::object& object = parsed.as_object();
  const auto data = object.if_contains("data");
  if (data == nullptr || !data->is_string()) {
    return std::nullopt;
  }

  return json::value_to<std::string>(*data);
}

auto ParsePairingRequest(const std::string& body) -> std::optional<PairingRequestPayload> {
  boost::system::error_code error_code;
  const json::value parsed = json::parse(body, error_code);
  if (error_code || !parsed.is_object()) {
    return std::nullopt;
  }

  const json::object& object = parsed.as_object();
  const auto device_name = object.if_contains("deviceName");
  const auto device_type = object.if_contains("deviceType");
  if (device_name == nullptr || device_type == nullptr || !device_name->is_string() ||
      !device_type->is_string()) {
    return std::nullopt;
  }

  const std::string device_type_name = json::value_to<std::string>(*device_type);
  vibe::auth::DeviceType parsed_device_type = vibe::auth::DeviceType::Unknown;
  if (device_type_name == "mobile") {
    parsed_device_type = vibe::auth::DeviceType::Mobile;
  } else if (device_type_name == "desktop") {
    parsed_device_type = vibe::auth::DeviceType::Desktop;
  } else if (device_type_name == "browser") {
    parsed_device_type = vibe::auth::DeviceType::Browser;
  }

  return PairingRequestPayload{
      .device_name = json::value_to<std::string>(*device_name),
      .device_type = parsed_device_type,
  };
}

auto ParsePairingApprovalRequest(const std::string& body) -> std::optional<PairingApprovalPayload> {
  boost::system::error_code error_code;
  const json::value parsed = json::parse(body, error_code);
  if (error_code || !parsed.is_object()) {
    return std::nullopt;
  }

  const json::object& object = parsed.as_object();
  const auto pairing_id = object.if_contains("pairingId");
  const auto code = object.if_contains("code");
  if (pairing_id == nullptr || code == nullptr || !pairing_id->is_string() || !code->is_string()) {
    return std::nullopt;
  }

  return PairingApprovalPayload{
      .pairing_id = json::value_to<std::string>(*pairing_id),
      .code = json::value_to<std::string>(*code),
  };
}

auto ParseHostConfigRequest(const std::string& body) -> std::optional<HostConfigPayload> {
  boost::system::error_code error_code;
  const json::value parsed = json::parse(body, error_code);
  if (error_code || !parsed.is_object()) {
    return std::nullopt;
  }

  const json::object& object = parsed.as_object();
  const auto display_name = object.if_contains("displayName");
  if (display_name == nullptr || !display_name->is_string()) {
    return std::nullopt;
  }

  return HostConfigPayload{
      .display_name = json::value_to<std::string>(*display_name),
  };
}

auto ParseWebSocketCommand(const std::string& body) -> std::optional<WebSocketCommand> {
  boost::system::error_code error_code;
  const json::value parsed = json::parse(body, error_code);
  if (error_code || !parsed.is_object()) {
    return std::nullopt;
  }

  const json::object& object = parsed.as_object();
  const auto type = object.if_contains("type");
  if (type == nullptr || !type->is_string()) {
    return std::nullopt;
  }

  const std::string command_type = json::value_to<std::string>(*type);
  if (command_type == "terminal.input") {
    const auto data = object.if_contains("data");
    if (data == nullptr || !data->is_string()) {
      return std::nullopt;
    }

    return WebSocketInputCommand{
        .data = json::value_to<std::string>(*data),
    };
  }

  if (command_type == "terminal.resize") {
    const auto cols = object.if_contains("cols");
    const auto rows = object.if_contains("rows");
    if (cols == nullptr || rows == nullptr || !cols->is_int64() || !rows->is_int64()) {
      return std::nullopt;
    }

    const std::int64_t cols_value = cols->as_int64();
    const std::int64_t rows_value = rows->as_int64();
    if (cols_value <= 0 || rows_value <= 0 ||
        cols_value > std::numeric_limits<std::uint16_t>::max() ||
        rows_value > std::numeric_limits<std::uint16_t>::max()) {
      return std::nullopt;
    }

    return WebSocketResizeCommand{
        .terminal_size =
            vibe::session::TerminalSize{
                .columns = static_cast<std::uint16_t>(cols_value),
                .rows = static_cast<std::uint16_t>(rows_value),
            },
    };
  }

  if (command_type == "session.stop") {
    return WebSocketStopCommand{};
  }

  if (command_type == "session.control.request") {
    vibe::session::ControllerKind controller_kind = vibe::session::ControllerKind::Remote;
    if (const auto kind = object.if_contains("kind"); kind != nullptr) {
      if (!kind->is_string()) {
        return std::nullopt;
      }

      const std::string kind_value = json::value_to<std::string>(*kind);
      if (kind_value == "host") {
        controller_kind = vibe::session::ControllerKind::Host;
      } else if (kind_value == "remote") {
        controller_kind = vibe::session::ControllerKind::Remote;
      } else {
        return std::nullopt;
      }
    }

    return WebSocketRequestControlCommand{
        .controller_kind = controller_kind,
    };
  }

  if (command_type == "session.control.release") {
    return WebSocketReleaseControlCommand{};
  }

  return std::nullopt;
}

auto ParseTailBytes(const std::string& target) -> std::size_t {
  constexpr std::size_t default_tail_bytes = 65536;
  const std::string marker = "?bytes=";
  const std::size_t marker_pos = target.find(marker);
  if (marker_pos == std::string::npos) {
    return default_tail_bytes;
  }

  const std::size_t value_start = marker_pos + marker.size();
  const std::string value = target.substr(value_start);
  if (value.empty()) {
    return default_tail_bytes;
  }

  return static_cast<std::size_t>(std::stoul(value));
}

}  // namespace vibe::net
