#include <gtest/gtest.h>

#include "vibe/net/json.h"

namespace vibe::net {
namespace {

TEST(HttpJsonTest, EscapesQuotedAndControlCharacters) {
  EXPECT_EQ(JsonEscape("a\"b\\c\n"), "a\\\"b\\\\c\\n");
}

TEST(HttpJsonTest, SerializesHostInfo) {
  const std::string json = ToJsonHostInfo();
  EXPECT_NE(json.find("\"hostId\":\"local-dev-host\""), std::string::npos);
  EXPECT_NE(json.find("\"adminHost\":\"127.0.0.1\""), std::string::npos);
  EXPECT_NE(json.find("\"adminPort\":18085"), std::string::npos);
  EXPECT_NE(json.find("\"remoteHost\":\"0.0.0.0\""), std::string::npos);
  EXPECT_NE(json.find("\"remotePort\":18086"), std::string::npos);
  EXPECT_NE(json.find("\"providerCommands\""), std::string::npos);
  EXPECT_NE(json.find("\"capabilities\":[\"sessions\",\"rest\",\"websocket\"]"),
            std::string::npos);
}

TEST(HttpJsonTest, SerializesSessionSummaryControllerFields) {
  const auto id = vibe::session::SessionId::TryCreate("s_host");
  ASSERT_TRUE(id.has_value());

  const std::string json = ToJson(vibe::service::SessionSummary{
      .id = *id,
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = ".",
      .title = "demo",
      .status = vibe::session::SessionStatus::Running,
      .controller_client_id = std::nullopt,
      .controller_kind = vibe::session::ControllerKind::Host,
      .is_recovered = false,
      .is_active = true,
      .created_at_unix_ms = 100,
      .last_status_at_unix_ms = 200,
  });

  EXPECT_NE(json.find("\"controllerKind\":\"host\""), std::string::npos);
  EXPECT_NE(json.find("\"activityState\":\"active\""), std::string::npos);
  EXPECT_NE(json.find("\"createdAtUnixMs\":100"), std::string::npos);
  EXPECT_NE(json.find("\"lastStatusAtUnixMs\":200"), std::string::npos);
}

TEST(HttpJsonTest, SerializesOutputSlice) {
  const std::string json = ToJson(vibe::session::OutputSlice{
      .seq_start = 3,
      .seq_end = 4,
      .data = "hello\n",
  });

  EXPECT_NE(json.find("\"seqStart\":3"), std::string::npos);
  EXPECT_NE(json.find("\"seqEnd\":4"), std::string::npos);
  EXPECT_NE(json.find("\"dataEncoding\":\"base64\""), std::string::npos);
  EXPECT_NE(json.find("\"dataBase64\":\"aGVsbG8K\""), std::string::npos);
}

TEST(HttpJsonTest, SerializesTerminalOutputEvent) {
  const std::string json = ToJson(TerminalOutputEvent{
      .session_id = "s_7",
      .slice =
          vibe::session::OutputSlice{
              .seq_start = 9,
              .seq_end = 10,
              .data = "tail",
          },
  });

  EXPECT_NE(json.find("\"type\":\"terminal.output\""), std::string::npos);
  EXPECT_NE(json.find("\"sessionId\":\"s_7\""), std::string::npos);
  EXPECT_NE(json.find("\"seqStart\":9"), std::string::npos);
  EXPECT_NE(json.find("\"seqEnd\":10"), std::string::npos);
  EXPECT_NE(json.find("\"dataEncoding\":\"base64\""), std::string::npos);
  EXPECT_NE(json.find("\"dataBase64\":\"dGFpbA==\""), std::string::npos);
}

TEST(HttpJsonTest, SerializesSessionUpdatedEvent) {
  const auto id = vibe::session::SessionId::TryCreate("s_9");
  ASSERT_TRUE(id.has_value());

  const std::string json = ToJson(SessionUpdatedEvent{
      .summary =
          vibe::service::SessionSummary{
              .id = *id,
              .provider = vibe::session::ProviderType::Codex,
              .workspace_root = "/tmp/project",
              .title = "demo",
              .status = vibe::session::SessionStatus::Running,
              .controller_client_id = "client-1",
              .controller_kind = vibe::session::ControllerKind::Remote,
              .is_recovered = false,
              .is_active = true,
              .created_at_unix_ms = 100,
              .last_status_at_unix_ms = 200,
          },
  });

  EXPECT_NE(json.find("\"type\":\"session.updated\""), std::string::npos);
  EXPECT_NE(json.find("\"sessionId\":\"s_9\""), std::string::npos);
  EXPECT_NE(json.find("\"status\":\"Running\""), std::string::npos);
  EXPECT_NE(json.find("\"controllerClientId\":\"client-1\""), std::string::npos);
  EXPECT_NE(json.find("\"controllerKind\":\"remote\""), std::string::npos);
  EXPECT_NE(json.find("\"activityState\":\"active\""), std::string::npos);
  EXPECT_NE(json.find("\"createdAtUnixMs\":100"), std::string::npos);
  EXPECT_NE(json.find("\"lastStatusAtUnixMs\":200"), std::string::npos);
}

TEST(HttpJsonTest, SerializesSessionExitedEvent) {
  const std::string json = ToJson(SessionExitedEvent{
      .session_id = "s_9",
      .status = vibe::session::SessionStatus::Exited,
  });

  EXPECT_NE(json.find("\"type\":\"session.exited\""), std::string::npos);
  EXPECT_NE(json.find("\"sessionId\":\"s_9\""), std::string::npos);
  EXPECT_NE(json.find("\"status\":\"Exited\""), std::string::npos);
}

TEST(HttpJsonTest, SerializesErrorEvent) {
  const std::string json = ToJson(ErrorEvent{
      .session_id = "s_9",
      .code = "invalid_command",
      .message = "invalid websocket command",
  });

  EXPECT_NE(json.find("\"type\":\"error\""), std::string::npos);
  EXPECT_NE(json.find("\"sessionId\":\"s_9\""), std::string::npos);
  EXPECT_NE(json.find("\"code\":\"invalid_command\""), std::string::npos);
  EXPECT_NE(json.find("\"message\":\"invalid websocket command\""), std::string::npos);
}

TEST(HttpJsonTest, SerializesAttachedClientInfo) {
  const std::string json = ToJson(AttachedClientInfo{
      .client_id = "ws_s_1_100",
      .session_id = "s_1",
      .session_title = "demo",
      .client_address = "127.0.0.1",
      .session_status = vibe::session::SessionStatus::Running,
      .session_is_recovered = false,
      .claimed_kind = vibe::session::ControllerKind::Remote,
      .is_local = false,
      .has_control = true,
      .connected_at_unix_ms = 300,
  });

  EXPECT_NE(json.find("\"clientId\":\"ws_s_1_100\""), std::string::npos);
  EXPECT_NE(json.find("\"sessionId\":\"s_1\""), std::string::npos);
  EXPECT_NE(json.find("\"sessionTitle\":\"demo\""), std::string::npos);
  EXPECT_NE(json.find("\"sessionStatus\":\"Running\""), std::string::npos);
  EXPECT_NE(json.find("\"claimedKind\":\"remote\""), std::string::npos);
  EXPECT_NE(json.find("\"hasControl\":true"), std::string::npos);
  EXPECT_NE(json.find("\"connectedAtUnixMs\":300"), std::string::npos);
}

}  // namespace
}  // namespace vibe::net
