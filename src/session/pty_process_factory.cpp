#include "vibe/session/pty_process_factory.h"

#if defined(__APPLE__) || defined(__linux__)
#include "vibe/session/posix_pty_process.h"
#endif

#include <memory>
#include <string_view>

namespace vibe::session {
namespace {

class UnsupportedPtyProcess final : public IPtyProcess {
 public:
  [[nodiscard]] auto Start(const LaunchSpec& /*launch_spec*/) -> StartResult override {
    const PtyPlatformSupport support = DetectPtyPlatformSupport();
    return StartResult{
        .started = false,
        .pid = 0,
        .error_message =
            std::string("native PTY runtime is unavailable on ") +
            std::string(support.platform_name),
    };
  }

  [[nodiscard]] auto Write(std::string_view /*input*/) -> bool override { return false; }
  [[nodiscard]] auto Read(int /*timeout_ms*/) -> ReadResult override {
    return ReadResult{.data = "", .closed = true};
  }
  [[nodiscard]] auto Resize(TerminalSize /*terminal_size*/) -> bool override { return false; }
  [[nodiscard]] auto PollExit() -> std::optional<int> override { return std::nullopt; }
  [[nodiscard]] auto Terminate() -> bool override { return false; }
};

}  // namespace

auto DetectPtyPlatformSupport() -> PtyPlatformSupport {
#if defined(__APPLE__)
  return PtyPlatformSupport{
      .platform = PtyPlatform::MacOS,
      .platform_name = "macOS",
      .backend_name = "posix-forkpty",
      .supports_native_pty = true,
      .detail = "using the shared POSIX forkpty backend",
  };
#elif defined(__linux__)
  return PtyPlatformSupport{
      .platform = PtyPlatform::Linux,
      .platform_name = "Linux",
      .backend_name = "posix-forkpty",
      .supports_native_pty = true,
      .detail = "using the shared POSIX forkpty backend",
  };
#else
  return PtyPlatformSupport{};
#endif
}

auto CreatePlatformPtyProcess() -> std::unique_ptr<IPtyProcess> {
#if defined(__APPLE__) || defined(__linux__)
  return std::make_unique<PosixPtyProcess>();
#else
  return std::make_unique<UnsupportedPtyProcess>();
#endif
}

}  // namespace vibe::session
