#include <gtest/gtest.h>

#include "vibe/net/websocket_shared.h"

namespace vibe::net {
namespace {

TEST(WebSocketSharedTest, DetectsSessionTarget) {
  EXPECT_TRUE(IsSessionWebSocketTarget("/ws/sessions/s_1"));
  EXPECT_TRUE(IsSessionWebSocketTarget("/ws/sessions/s_1?access_token=abc"));
  EXPECT_FALSE(IsSessionWebSocketTarget("/sessions/s_1"));
}

TEST(WebSocketSharedTest, ExtractsSessionId) {
  EXPECT_EQ(ExtractSessionIdFromWebSocketTarget("/ws/sessions/s_123"), "s_123");
  EXPECT_EQ(ExtractSessionIdFromWebSocketTarget("/ws/sessions/s_123?access_token=abc"), "s_123");
  EXPECT_TRUE(ExtractSessionIdFromWebSocketTarget("/sessions/s_123").empty());
}

TEST(WebSocketSharedTest, ExtractsAccessToken) {
  EXPECT_EQ(ExtractAccessTokenFromWebSocketTarget("/ws/sessions/s_123?access_token=abc123"), "abc123");
  EXPECT_EQ(ExtractAccessTokenFromWebSocketTarget("/ws/sessions/s_123?foo=bar&access_token=xyz"), "xyz");
  EXPECT_TRUE(ExtractAccessTokenFromWebSocketTarget("/ws/sessions/s_123").empty());
}

}  // namespace
}  // namespace vibe::net
