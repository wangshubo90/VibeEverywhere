#include <gtest/gtest.h>

#include "vibe/session/session_types.h"

namespace vibe::session {
namespace {

TEST(SessionIdTest, AcceptsSimpleIdentifiers) {
  const auto session_id = SessionId::TryCreate("refactor-ui_01");

  ASSERT_TRUE(session_id.has_value());
  EXPECT_EQ(session_id->value(), "refactor-ui_01");
}

TEST(SessionIdTest, RejectsEmptyOrUnsafeIdentifiers) {
  EXPECT_FALSE(SessionId::TryCreate("").has_value());
  EXPECT_FALSE(SessionId::TryCreate("has space").has_value());
  EXPECT_FALSE(SessionId::TryCreate("../escape").has_value());
  EXPECT_FALSE(SessionId::TryCreate("semi;colon").has_value());
}

TEST(SessionTypesTest, ExposesStableProviderNames) {
  EXPECT_EQ(ToString(ProviderType::Codex), "codex");
  EXPECT_EQ(ToString(ProviderType::Claude), "claude");
}

TEST(SessionTypesTest, ExposesStableControllerNames) {
  EXPECT_EQ(ToString(ControllerKind::None), "none");
  EXPECT_EQ(ToString(ControllerKind::Host), "host");
  EXPECT_EQ(ToString(ControllerKind::Remote), "remote");
}

TEST(SessionMetadataTest, DefaultsToCreatedStatus) {
  const auto session_id = SessionId::TryCreate("session_123");
  ASSERT_TRUE(session_id.has_value());

  const SessionMetadata metadata{
      .id = *session_id,
      .provider = ProviderType::Codex,
      .workspace_root = "/tmp/project",
      .title = "bootstrap",
      .status = SessionStatus::Created,
      .conversation_id = std::nullopt,
      .group_tags = {},
  };

  EXPECT_EQ(metadata.id.value(), "session_123");
  EXPECT_EQ(metadata.provider, ProviderType::Codex);
  EXPECT_EQ(metadata.workspace_root, "/tmp/project");
  EXPECT_EQ(metadata.title, "bootstrap");
  EXPECT_EQ(metadata.status, SessionStatus::Created);
}

}  // namespace
}  // namespace vibe::session
