#include <gtest/gtest.h>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <chrono>
#include <thread>

#include "vibe/cli/daemon_client.h"

namespace vibe::cli {
namespace {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

TEST(DaemonClientTest, BuildsCreateSessionRequestBody) {
  const std::string body = BuildCreateSessionRequestBody(CreateSessionRequest{
      .provider = vibe::session::ProviderType::Codex,
      .workspace_root = "/tmp/project",
      .title = "demo",
      .record_id = std::nullopt,
      .command_argv = std::nullopt,
      .command_shell = std::nullopt,
  });

  EXPECT_NE(body.find("\"provider\":\"codex\""), std::string::npos);
  EXPECT_NE(body.find("\"workspaceRoot\":\"/tmp/project\""), std::string::npos);
  EXPECT_NE(body.find("\"title\":\"demo\""), std::string::npos);
}

TEST(DaemonClientTest, BuildsCreateSessionRequestBodyWithRecordIdAndShellCommand) {
  const std::string body = BuildCreateSessionRequestBody(CreateSessionRequest{
      .provider = std::nullopt,
      .workspace_root = "/tmp/project",
      .title = "demo",
      .record_id = "rec_1",
      .command_argv = std::nullopt,
      .command_shell = "codex \"$(cat prompt.md)\"",
  });

  EXPECT_NE(body.find("\"recordId\":\"rec_1\""), std::string::npos);
  EXPECT_NE(body.find("\"commandShell\":\"codex \\\"$(cat prompt.md)\\\"\""), std::string::npos);
  EXPECT_EQ(body.find("\"provider\":"), std::string::npos);
}

TEST(DaemonClientTest, ParsesCreatedSessionId) {
  const auto session_id = ParseCreatedSessionId(R"({"sessionId":"s_42","status":"Running"})");
  ASSERT_TRUE(session_id.has_value());
  EXPECT_EQ(*session_id, "s_42");
}

TEST(DaemonClientTest, BuildsRelayRequestBody) {
  const std::string body = BuildRelayRequestBody("h_123", "s_456");
  EXPECT_NE(body.find("\"host_id\":\"h_123\""), std::string::npos);
  EXPECT_NE(body.find("\"session_id\":\"s_456\""), std::string::npos);
}

TEST(DaemonClientTest, ParsesRelayChannelId) {
  const auto channel_id = ParseRelayChannelId(R"({"channel_id":"ch_42"})");
  ASSERT_TRUE(channel_id.has_value());
  EXPECT_EQ(*channel_id, "ch_42");
  EXPECT_FALSE(ParseRelayChannelId("{}").has_value());
}

TEST(DaemonClientTest, ParsesSessionList) {
  const auto sessions = ParseSessionList(
      R"([{"sessionId":"s_1","title":"one","activityState":"active","status":"Running"},{"sessionId":"s_2","title":"two","activityState":"stopped","status":"Exited"}])");

  ASSERT_EQ(sessions.size(), 2U);
  EXPECT_EQ(sessions[0].session_id, "s_1");
  EXPECT_EQ(sessions[0].title, "one");
  EXPECT_EQ(sessions[0].activity_state, "active");
  EXPECT_EQ(sessions[0].status, "Running");
  EXPECT_EQ(sessions[1].session_id, "s_2");
  EXPECT_EQ(sessions[1].title, "two");
}

TEST(DaemonClientTest, ParsesSessionListWithAdditiveNodeSummaryFields) {
  const auto sessions = ParseSessionList(
      R"([{"sessionId":"s_1","title":"one","activityState":"active","status":"Running","interactionKind":"running_non_interactive","semanticPreview":"Workspace dirty","nodeSummary":{"sessionId":"s_1","lifecycleStatus":"Running","interactionKind":"running_non_interactive","attentionState":"info","semanticPreview":"Workspace dirty","recentFileChangeCount":2,"gitDirty":true}}])");

  ASSERT_EQ(sessions.size(), 1U);
  EXPECT_EQ(sessions[0].session_id, "s_1");
  EXPECT_EQ(sessions[0].title, "one");
  EXPECT_EQ(sessions[0].activity_state, "active");
  EXPECT_EQ(sessions[0].status, "Running");
  EXPECT_EQ(sessions[0].interaction_kind, "running_non_interactive");
  EXPECT_EQ(sessions[0].semantic_preview, "Workspace dirty");
}

TEST(DaemonClientTest, ParsesRecordList) {
  const auto records = ParseRecordList(
      R"([{"recordId":"rec_1","provider":"codex","workspaceRoot":".","title":"prompt","launchedAtUnixMs":1700000000000,"conversationId":"conv-1","groupTags":["frontend"],"commandShell":"codex \"$(cat prompt.md)\""},{"recordId":"rec_2","provider":"claude","workspaceRoot":"/tmp","title":"ops","launchedAtUnixMs":1700000001000,"commandArgv":["/bin/bash","-l"]}])");

  ASSERT_EQ(records.size(), 2U);
  EXPECT_EQ(records[0].record_id, "rec_1");
  EXPECT_EQ(records[0].launched_at_unix_ms, 1700000000000LL);
  ASSERT_TRUE(records[0].conversation_id.has_value());
  EXPECT_EQ(*records[0].conversation_id, "conv-1");
  ASSERT_TRUE(records[0].command_shell.has_value());
  EXPECT_EQ(*records[0].command_shell, "codex \"$(cat prompt.md)\"");
  ASSERT_TRUE(records[1].command_argv.has_value());
  EXPECT_EQ(*records[1].command_argv, (std::vector<std::string>{"/bin/bash", "-l"}));
}

TEST(DaemonClientTest, BuildsControlAndTerminalCommands) {
  const std::string control = BuildControlRequestCommand(vibe::session::ControllerKind::Host);
  EXPECT_NE(control.find("\"type\":\"session.control.request\""), std::string::npos);
  EXPECT_NE(control.find("\"kind\":\"host\""), std::string::npos);

  const std::string release = BuildReleaseControlCommand();
  EXPECT_NE(release.find("\"type\":\"session.control.release\""), std::string::npos);

  const std::string input = BuildInputCommand("hello\n");
  EXPECT_NE(input.find("\"type\":\"terminal.input\""), std::string::npos);
  EXPECT_NE(input.find("\"data\":\"hello\\n\""), std::string::npos);

  const std::string resize = BuildResizeCommand(
      vibe::session::TerminalSize{.columns = 90, .rows = 30});
  EXPECT_NE(resize.find("\"type\":\"terminal.resize\""), std::string::npos);
  EXPECT_NE(resize.find("\"cols\":90"), std::string::npos);
  EXPECT_NE(resize.find("\"rows\":30"), std::string::npos);
}

TEST(DaemonClientTest, RejectsMalformedCreateSessionResponse) {
  EXPECT_FALSE(ParseCreatedSessionId("{}").has_value());
  EXPECT_FALSE(ParseCreatedSessionId("not-json").has_value());
}

TEST(DaemonClientTest, ReturnsNulloptWhenDaemonIsUnavailable) {
  asio::io_context io_context;
  tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 0));
  const auto port = acceptor.local_endpoint().port();
  acceptor.close();

  EXPECT_NO_THROW({
    const auto session_id = CreateSession(
        DaemonEndpoint{.host = "127.0.0.1", .port = port},
        CreateSessionRequest{
            .provider = vibe::session::ProviderType::Codex,
            .workspace_root = "/tmp/project",
            .title = "demo",
            .record_id = std::nullopt,
            .command_argv = std::nullopt,
            .command_shell = std::nullopt,
        });
    EXPECT_FALSE(session_id.has_value());
  });
}

TEST(DaemonClientTest, ReturnsNulloptWhenDaemonAcceptsButDoesNotRespond) {
  asio::io_context io_context;
  tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 0));
  const auto port = acceptor.local_endpoint().port();

  std::thread server_thread([&acceptor]() {
    tcp::socket socket(acceptor.get_executor());
    boost::system::error_code error_code;
    acceptor.accept(socket, error_code);
    if (error_code) {
      return;
    }
    std::array<char, 1024> request_buffer{};
    socket.read_some(asio::buffer(request_buffer), error_code);
    if (error_code) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::seconds(5));
  });

  const auto started_at = std::chrono::steady_clock::now();
  const auto host_info = GetHostInfo(DaemonEndpoint{.host = "127.0.0.1", .port = port});
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                            started_at);

  EXPECT_FALSE(host_info.has_value());
  EXPECT_LT(elapsed, std::chrono::seconds(4));

  acceptor.close();
  server_thread.join();
}

}  // namespace
}  // namespace vibe::cli
