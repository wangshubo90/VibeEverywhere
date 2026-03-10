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
  });

  EXPECT_NE(json.find("\"controllerKind\":\"host\""), std::string::npos);
}

TEST(HttpJsonTest, SerializesOutputSlice) {
  const std::string json = ToJson(vibe::session::OutputSlice{
      .seq_start = 3,
      .seq_end = 4,
      .data = "hello\n",
  });

  EXPECT_NE(json.find("\"seqStart\":3"), std::string::npos);
  EXPECT_NE(json.find("\"seqEnd\":4"), std::string::npos);
  EXPECT_NE(json.find("\"data\":\"hello\\n\""), std::string::npos);
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
  EXPECT_NE(json.find("\"data\":\"tail\""), std::string::npos);
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
          },
  });

  EXPECT_NE(json.find("\"type\":\"session.updated\""), std::string::npos);
  EXPECT_NE(json.find("\"sessionId\":\"s_9\""), std::string::npos);
  EXPECT_NE(json.find("\"status\":\"Running\""), std::string::npos);
  EXPECT_NE(json.find("\"controllerClientId\":\"client-1\""), std::string::npos);
  EXPECT_NE(json.find("\"controllerKind\":\"remote\""), std::string::npos);
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

}  // namespace
}  // namespace vibe::net
