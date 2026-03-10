#include <gtest/gtest.h>

#include "vibe/net/request_parsing.h"

namespace vibe::net {
namespace {

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

}  // namespace
}  // namespace vibe::net
