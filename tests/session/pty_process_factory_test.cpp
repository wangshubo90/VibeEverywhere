#include <gtest/gtest.h>

#include "vibe/session/pty_process_factory.h"

namespace vibe::session {
namespace {

TEST(PtyProcessFactoryTest, DetectsCurrentPlatformSupport) {
  const PtyPlatformSupport support = DetectPtyPlatformSupport();

#if defined(__APPLE__)
  EXPECT_EQ(support.platform, PtyPlatform::MacOS);
  EXPECT_EQ(support.platform_name, "macOS");
  EXPECT_TRUE(support.supports_native_pty);
  EXPECT_EQ(support.backend_name, "posix-forkpty");
#elif defined(__linux__)
  EXPECT_EQ(support.platform, PtyPlatform::Linux);
  EXPECT_EQ(support.platform_name, "Linux");
  EXPECT_TRUE(support.supports_native_pty);
  EXPECT_EQ(support.backend_name, "posix-forkpty");
#else
  EXPECT_EQ(support.platform, PtyPlatform::Unsupported);
  EXPECT_FALSE(support.supports_native_pty);
#endif
}

TEST(PtyProcessFactoryTest, CreatesProcessObjectForPlatformBackend) {
  auto process = CreatePlatformPtyProcess();
  ASSERT_NE(process, nullptr);
}

}  // namespace
}  // namespace vibe::session
