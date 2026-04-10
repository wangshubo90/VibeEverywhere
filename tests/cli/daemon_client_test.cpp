#include <gtest/gtest.h>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

#include "vibe/cli/daemon_client.h"

namespace vibe::cli {
namespace {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

TEST(DaemonClientTest, BuildsCreateSessionRequestBody) {
  const std::string body =
      BuildCreateSessionRequestBody(vibe::session::ProviderType::Codex, "/tmp/project", "demo");

  EXPECT_NE(body.find("\"provider\":\"codex\""), std::string::npos);
  EXPECT_NE(body.find("\"workspaceRoot\":\"/tmp/project\""), std::string::npos);
  EXPECT_NE(body.find("\"title\":\"demo\""), std::string::npos);
}

TEST(DaemonClientTest, ParsesCreatedSessionId) {
  const auto session_id = ParseCreatedSessionId(R"({"sessionId":"s_42","status":"Running"})");
  ASSERT_TRUE(session_id.has_value());
  EXPECT_EQ(*session_id, "s_42");
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
    const auto session_id =
        CreateSession(DaemonEndpoint{.host = "127.0.0.1", .port = port},
                      vibe::session::ProviderType::Codex, "/tmp/project", "demo");
    EXPECT_FALSE(session_id.has_value());
  });
}

}  // namespace
}  // namespace vibe::cli
