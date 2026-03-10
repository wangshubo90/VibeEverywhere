#include <gtest/gtest.h>

#include "vibe/net/websocket_shared.h"

namespace vibe::net {
namespace {

TEST(WebSocketSharedTest, DetectsSessionTarget) {
  EXPECT_TRUE(IsSessionWebSocketTarget("/ws/sessions/s_1"));
  EXPECT_FALSE(IsSessionWebSocketTarget("/sessions/s_1"));
}

TEST(WebSocketSharedTest, ExtractsSessionId) {
  EXPECT_EQ(ExtractSessionIdFromWebSocketTarget("/ws/sessions/s_123"), "s_123");
  EXPECT_TRUE(ExtractSessionIdFromWebSocketTarget("/sessions/s_123").empty());
}

}  // namespace
}  // namespace vibe::net
