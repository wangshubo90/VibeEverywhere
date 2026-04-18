#include <gtest/gtest.h>

#include "vibe/net/request_parsing.h"

namespace vibe::net {
namespace {

TEST(RequestParsingTest, ParsesCreateSessionRequestWithExplicitCommand) {
  const auto request = ParseCreateSessionRequest(
      R"({"provider":"claude","workspaceRoot":".","title":"demo","command":["/opt/homebrew/bin/claude","--print"]})");
  ASSERT_TRUE(request.has_value());
  ASSERT_TRUE(request->provider.has_value());
  EXPECT_EQ(*request->provider, vibe::session::ProviderType::Claude);
  ASSERT_TRUE(request->command_argv.has_value());
  EXPECT_EQ(*request->command_argv,
            (std::vector<std::string>{"/opt/homebrew/bin/claude", "--print"}));
  EXPECT_FALSE(request->command_shell.has_value());
}

TEST(RequestParsingTest, ParsesCreateSessionRequestWithConversationId) {
  const auto request = ParseCreateSessionRequest(
      R"({"provider":"codex","workspaceRoot":".","title":"demo","conversationId":"conv_123"})");
  ASSERT_TRUE(request.has_value());
  ASSERT_TRUE(request->conversation_id.has_value());
  EXPECT_EQ(*request->conversation_id, "conv_123");
}

TEST(RequestParsingTest, ParsesCreateSessionRequestWithNormalizedGroupTags) {
  const auto request = ParseCreateSessionRequest(
      R"({"provider":"codex","workspaceRoot":".","title":"demo","groupTags":[" Frontend ","mvp","frontend"]})");
  ASSERT_TRUE(request.has_value());
  ASSERT_TRUE(request->group_tags.has_value());
  EXPECT_EQ(*request->group_tags, (std::vector<std::string>{"frontend", "mvp"}));
}

TEST(RequestParsingTest, ParsesCreateSessionRequestWithShellCommandAndRecordId) {
  const auto request = ParseCreateSessionRequest(
      R"({"recordId":"rec_1","commandShell":"codex \"$(cat prompt.md)\""})");
  ASSERT_TRUE(request.has_value());
  ASSERT_TRUE(request->record_id.has_value());
  EXPECT_EQ(*request->record_id, "rec_1");
  ASSERT_TRUE(request->command_shell.has_value());
  EXPECT_EQ(*request->command_shell, "codex \"$(cat prompt.md)\"");
  EXPECT_FALSE(request->command_argv.has_value());
  EXPECT_FALSE(request->provider.has_value());
}

TEST(RequestParsingTest, ParsesCreateSessionRequestWithEnvironmentFields) {
  const auto request = ParseCreateSessionRequest(
      R"({"provider":"codex","workspaceRoot":".","title":"demo","envMode":"clean","environmentOverrides":{"FOO":"bar","HELLO":"world"},"envFilePath":".env.local"})");
  ASSERT_TRUE(request.has_value());
  ASSERT_TRUE(request->env_mode.has_value());
  EXPECT_EQ(*request->env_mode, vibe::session::EnvMode::Clean);
  EXPECT_EQ(request->environment_overrides.at("FOO"), "bar");
  EXPECT_EQ(request->environment_overrides.at("HELLO"), "world");
  ASSERT_TRUE(request->env_file_path.has_value());
  EXPECT_EQ(*request->env_file_path, ".env.local");
}

TEST(RequestParsingTest, RejectsInvalidExplicitCommandInCreateSessionRequest) {
  EXPECT_FALSE(ParseCreateSessionRequest(
                   R"({"provider":"codex","workspaceRoot":".","title":"demo","command":[]})")
                   .has_value());
  EXPECT_FALSE(ParseCreateSessionRequest(
                   R"({"provider":"codex","workspaceRoot":".","title":"demo","command":[""]})")
                   .has_value());
  EXPECT_FALSE(ParseCreateSessionRequest(
                   R"({"provider":"codex","workspaceRoot":".","title":"demo","command":"codex"})")
                   .has_value());
  EXPECT_FALSE(ParseCreateSessionRequest(
                   R"({"provider":"codex","workspaceRoot":".","title":"demo","command":["codex"],"commandShell":"codex"})")
                   .has_value());
}

TEST(RequestParsingTest, RejectsEmptyGroupTagsInCreateSessionRequest) {
  EXPECT_FALSE(ParseCreateSessionRequest(
                   R"({"provider":"codex","workspaceRoot":".","title":"demo","groupTags":[""]})")
                   .has_value());
  EXPECT_FALSE(ParseCreateSessionRequest(
                   R"({"provider":"codex","workspaceRoot":".","title":"demo","groupTags":["   "]})")
                   .has_value());
}

TEST(RequestParsingTest, ParsesHostConfigRequestWithListenerAndProviderOverrides) {
  const auto payload = ParseHostConfigRequest(
      R"({"displayName":"Host Box","adminHost":"127.0.0.1","adminPort":19085,"remoteHost":"192.168.1.10","remotePort":19086,"providerCommands":{"codex":["/opt/bin/codex","--fast"],"claude":["/opt/bin/claude","--print"]}})");
  ASSERT_TRUE(payload.has_value());
  EXPECT_EQ(payload->display_name, "Host Box");
  EXPECT_EQ(payload->admin_host, "127.0.0.1");
  EXPECT_EQ(payload->admin_port, 19085);
  EXPECT_EQ(payload->remote_host, "192.168.1.10");
  EXPECT_EQ(payload->remote_port, 19086);
  ASSERT_TRUE(payload->codex_command.has_value());
  EXPECT_EQ(*payload->codex_command,
            (std::vector<std::string>{"/opt/bin/codex", "--fast"}));
  ASSERT_TRUE(payload->claude_command.has_value());
  EXPECT_EQ(*payload->claude_command,
            (std::vector<std::string>{"/opt/bin/claude", "--print"}));
}


TEST(RequestParsingTest, RejectsInvalidHostConfigRequest) {
  EXPECT_FALSE(ParseHostConfigRequest(
                   R"({"displayName":"Host Box","adminHost":"","adminPort":19085,"remoteHost":"0.0.0.0","remotePort":19086})")
                   .has_value());
  EXPECT_FALSE(ParseHostConfigRequest(
                   R"({"displayName":"Host Box","adminHost":"127.0.0.1","adminPort":0,"remoteHost":"0.0.0.0","remotePort":19086})")
                   .has_value());
  EXPECT_FALSE(ParseHostConfigRequest(
                   R"({"displayName":"Host Box","adminHost":"127.0.0.1","adminPort":19085,"remoteHost":"0.0.0.0","remotePort":19086,"providerCommands":{"codex":[]}})")
                   .has_value());
}

TEST(RequestParsingTest, ParsesSessionGroupTagsUpdateRequest) {
  const auto payload =
      ParseSessionGroupTagsUpdateRequest(R"({"mode":"remove","tags":[" Frontend ","frontend","mvp"]})");
  ASSERT_TRUE(payload.has_value());
  EXPECT_EQ(payload->mode, vibe::service::SessionGroupTagsUpdateMode::Remove);
  EXPECT_EQ(payload->tags, (std::vector<std::string>{"frontend", "mvp"}));
}

TEST(RequestParsingTest, RejectsInvalidSessionGroupTagsUpdateRequest) {
  EXPECT_FALSE(ParseSessionGroupTagsUpdateRequest(R"({"mode":"invalid","tags":["frontend"]})").has_value());
  EXPECT_FALSE(ParseSessionGroupTagsUpdateRequest(R"({"mode":"add","tags":["   "]})").has_value());
  EXPECT_FALSE(ParseSessionGroupTagsUpdateRequest(R"({"mode":"set","tags":"frontend"})").has_value());
}

TEST(RequestParsingTest, ParsesWebSocketInputCommand) {
  const auto command = ParseWebSocketCommand(R"({"type":"terminal.input","data":"hello\n"})");
  ASSERT_TRUE(command.has_value());
  ASSERT_TRUE(std::holds_alternative<WebSocketInputCommand>(*command));
  EXPECT_EQ(std::get<WebSocketInputCommand>(*command).data, "hello\n");
}

TEST(RequestParsingTest, ParsesWebSocketResizeCommand) {
  const auto command = ParseWebSocketCommand(R"({"type":"terminal.resize","cols":80,"rows":24})");
  ASSERT_TRUE(command.has_value());
  ASSERT_TRUE(std::holds_alternative<WebSocketResizeCommand>(*command));
  EXPECT_EQ(std::get<WebSocketResizeCommand>(*command).terminal_size.columns, 80);
  EXPECT_EQ(std::get<WebSocketResizeCommand>(*command).terminal_size.rows, 24);
}

TEST(RequestParsingTest, ParsesWebSocketStopCommand) {
  const auto command = ParseWebSocketCommand(R"({"type":"session.stop"})");
  ASSERT_TRUE(command.has_value());
  EXPECT_TRUE(std::holds_alternative<WebSocketStopCommand>(*command));
}

TEST(RequestParsingTest, ParsesControlCommands) {
  const auto request_control = ParseWebSocketCommand(R"({"type":"session.control.request"})");
  ASSERT_TRUE(request_control.has_value());
  EXPECT_TRUE(std::holds_alternative<WebSocketRequestControlCommand>(*request_control));
  EXPECT_EQ(std::get<WebSocketRequestControlCommand>(*request_control).controller_kind,
            vibe::session::ControllerKind::Remote);

  const auto request_host_control =
      ParseWebSocketCommand(R"({"type":"session.control.request","kind":"host"})");
  ASSERT_TRUE(request_host_control.has_value());
  ASSERT_TRUE(std::holds_alternative<WebSocketRequestControlCommand>(*request_host_control));
  EXPECT_EQ(std::get<WebSocketRequestControlCommand>(*request_host_control).controller_kind,
            vibe::session::ControllerKind::Host);

  const auto release_control = ParseWebSocketCommand(R"({"type":"session.control.release"})");
  ASSERT_TRUE(release_control.has_value());
  EXPECT_TRUE(std::holds_alternative<WebSocketReleaseControlCommand>(*release_control));
}

TEST(RequestParsingTest, RejectsMalformedOrUnknownWebSocketCommands) {
  EXPECT_FALSE(ParseWebSocketCommand(R"({"type":"unknown"})").has_value());
  EXPECT_FALSE(ParseWebSocketCommand(R"({"type":"terminal.input"})").has_value());
  EXPECT_FALSE(ParseWebSocketCommand(R"({"type":"terminal.resize","cols":0,"rows":24})").has_value());
  EXPECT_FALSE(ParseWebSocketCommand("not-json").has_value());
}

TEST(RequestParsingTest, ParsesTailAndFileByteLimitsSafely) {
  EXPECT_EQ(ParseTailBytes("/sessions/s_1/tail"), std::optional<std::size_t>{65536U});
  EXPECT_EQ(ParseTailBytes("/sessions/s_1/tail?bytes=128"), std::optional<std::size_t>{128U});
  EXPECT_FALSE(ParseTailBytes("/sessions/s_1/tail?bytes=abc").has_value());
  EXPECT_FALSE(ParseTailBytes("/sessions/s_1/tail?bytes=-1").has_value());

  EXPECT_EQ(ParseFileBytes("/sessions/s_1/file?path=src%2Fmain.cpp"),
            std::optional<std::size_t>{65536U});
  EXPECT_EQ(ParseFileBytes("/sessions/s_1/file?path=src%2Fmain.cpp&bytes=512"),
            std::optional<std::size_t>{512U});
  EXPECT_EQ(ParseFileBytes("/sessions/s_1/file?path=src%2Fmain.cpp&bytes=2097152"),
            std::optional<std::size_t>{1048576U});
  EXPECT_FALSE(ParseFileBytes("/sessions/s_1/file?path=src%2Fmain.cpp&bytes=abc").has_value());
  EXPECT_FALSE(ParseFileBytes("/sessions/s_1/file?path=src%2Fmain.cpp&bytes=-1").has_value());
}

}  // namespace
}  // namespace vibe::net
