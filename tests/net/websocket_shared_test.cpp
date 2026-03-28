#include <gtest/gtest.h>

#include "vibe/net/websocket_shared.h"

namespace vibe::net {
namespace {

TEST(WebSocketSharedTest, DetectsSessionTarget) {
  EXPECT_TRUE(IsSessionWebSocketTarget("/ws/sessions/s_1"));
  EXPECT_TRUE(IsSessionWebSocketTarget("/ws/sessions/s_1?access_token=abc"));
  EXPECT_FALSE(IsSessionWebSocketTarget("/sessions/s_1"));
}

TEST(WebSocketSharedTest, DetectsOverviewTarget) {
  EXPECT_TRUE(IsOverviewWebSocketTarget("/ws/overview"));
  EXPECT_TRUE(IsOverviewWebSocketTarget("/ws/overview?access_token=abc"));
  EXPECT_FALSE(IsOverviewWebSocketTarget("/ws/sessions/s_1"));
}

TEST(WebSocketSharedTest, ExtractsSessionId) {
  EXPECT_EQ(ExtractSessionIdFromWebSocketTarget("/ws/sessions/s_123"), "s_123");
  EXPECT_EQ(ExtractSessionIdFromWebSocketTarget("/ws/sessions/s_123?access_token=abc"), "s_123");
  EXPECT_TRUE(ExtractSessionIdFromWebSocketTarget("/sessions/s_123").empty());
}

TEST(WebSocketSharedTest, ExtractsAccessToken) {
  EXPECT_EQ(ExtractAccessTokenFromWebSocketTarget("/ws/sessions/s_123?access_token=abc123"), "abc123");
  EXPECT_EQ(ExtractAccessTokenFromWebSocketTarget("/ws/sessions/s_123?foo=bar&access_token=xyz"), "xyz");
  EXPECT_EQ(ExtractAccessTokenFromWebSocketTarget("/ws/overview?access_token=xyz"), "xyz");
  EXPECT_TRUE(ExtractAccessTokenFromWebSocketTarget("/ws/sessions/s_123").empty());
}

TEST(WebSocketSharedTest, StreamSequenceWindowReservesUndeliveredOutput) {
  StreamSequenceWindow window;

  EXPECT_EQ(window.delivered_next(), 1U);
  EXPECT_EQ(window.next_request_sequence(), 1U);

  window.ReserveThrough(8);
  EXPECT_EQ(window.delivered_next(), 1U);
  EXPECT_EQ(window.next_request_sequence(), 8U);

  window.MarkDelivered(4);
  EXPECT_EQ(window.delivered_next(), 4U);
  EXPECT_EQ(window.next_request_sequence(), 8U);

  window.MarkDelivered(8);
  EXPECT_EQ(window.delivered_next(), 8U);
  EXPECT_EQ(window.next_request_sequence(), 8U);
}

}  // namespace
}  // namespace vibe::net
