#include "vibe/net/request_parsing.h"

#include <limits>

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
  const auto command = object.if_contains("command");

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

  std::optional<std::vector<std::string>> command_argv = std::nullopt;
  if (command != nullptr) {
    if (!command->is_array()) {
      return std::nullopt;
    }

    std::vector<std::string> parsed_command;
    for (const auto& item : command->as_array()) {
      if (!item.is_string()) {
        return std::nullopt;
      }
      const std::string argument = json::value_to<std::string>(item);
      if (argument.empty()) {
        return std::nullopt;
      }
      parsed_command.push_back(argument);
    }

    if (parsed_command.empty()) {
      return std::nullopt;
    }
    command_argv = std::move(parsed_command);
  }

  return vibe::service::CreateSessionRequest{
      .provider = provider,
      .workspace_root = json::value_to<std::string>(*workspace_root),
      .title = json::value_to<std::string>(*title),
      .command_argv = std::move(command_argv),
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
  const auto admin_host = object.if_contains("adminHost");
  const auto admin_port = object.if_contains("adminPort");
  const auto remote_host = object.if_contains("remoteHost");
  const auto remote_port = object.if_contains("remotePort");
  if (display_name == nullptr || admin_host == nullptr || admin_port == nullptr ||
      remote_host == nullptr || remote_port == nullptr || !display_name->is_string() ||
      !admin_host->is_string() || !admin_port->is_int64() || !remote_host->is_string() ||
      !remote_port->is_int64()) {
    return std::nullopt;
  }

  const auto parse_port = [](const json::value& value) -> std::optional<std::uint16_t> {
    if (!value.is_int64()) {
      return std::nullopt;
    }
    const auto port = value.as_int64();
    if (port <= 0 || port > std::numeric_limits<std::uint16_t>::max()) {
      return std::nullopt;
    }
    return static_cast<std::uint16_t>(port);
  };

  const auto parse_command = [](const json::value* value) -> std::optional<std::vector<std::string>> {
    if (value == nullptr) {
      return std::nullopt;
    }
    if (!value->is_array()) {
      return std::nullopt;
    }

    const json::array& array = value->as_array();
    if (array.empty()) {
      return std::nullopt;
    }

    std::vector<std::string> command;
    command.reserve(array.size());
    for (const auto& token : array) {
      if (!token.is_string()) {
        return std::nullopt;
      }
      const std::string value_string = json::value_to<std::string>(token);
      if (value_string.empty()) {
        return std::nullopt;
      }
      command.push_back(value_string);
    }
    return command;
  };

  const std::string parsed_admin_host = json::value_to<std::string>(*admin_host);
  const std::string parsed_remote_host = json::value_to<std::string>(*remote_host);
  const auto parsed_admin_port = parse_port(*admin_port);
  const auto parsed_remote_port = parse_port(*remote_port);
  if (parsed_admin_host.empty() || parsed_remote_host.empty() ||
      !parsed_admin_port.has_value() || !parsed_remote_port.has_value()) {
    return std::nullopt;
  }

  std::optional<std::vector<std::string>> codex_command = std::nullopt;
  std::optional<std::vector<std::string>> claude_command = std::nullopt;
  if (const auto provider_commands = object.if_contains("providerCommands");
      provider_commands != nullptr) {
    if (!provider_commands->is_object()) {
      return std::nullopt;
    }

    const json::object& provider_commands_object = provider_commands->as_object();
    if (provider_commands_object.if_contains("codex") != nullptr) {
      codex_command = parse_command(provider_commands_object.if_contains("codex"));
      if (!codex_command.has_value()) {
        return std::nullopt;
      }
    }
    if (provider_commands_object.if_contains("claude") != nullptr) {
      claude_command = parse_command(provider_commands_object.if_contains("claude"));
      if (!claude_command.has_value()) {
        return std::nullopt;
      }
    }
  }

  return HostConfigPayload{
      .display_name = json::value_to<std::string>(*display_name),
      .admin_host = parsed_admin_host,
      .admin_port = *parsed_admin_port,
      .remote_host = parsed_remote_host,
      .remote_port = *parsed_remote_port,
      .codex_command = std::move(codex_command),
      .claude_command = std::move(claude_command),
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
