#ifndef VIBE_SESSION_PTY_PROCESS_FACTORY_H
#define VIBE_SESSION_PTY_PROCESS_FACTORY_H

#include <memory>
#include <string_view>

#include "vibe/session/pty_process.h"

namespace vibe::session {

enum class PtyPlatform {
  MacOS,
  Linux,
  Unsupported,
};

struct PtyPlatformSupport {
  PtyPlatform platform{PtyPlatform::Unsupported};
  std::string_view platform_name{"unsupported"};
  std::string_view backend_name{"unsupported"};
  bool supports_native_pty{false};
  std::string_view detail{"native PTY runtime is only wired for macOS and Linux"};
};

[[nodiscard]] auto DetectPtyPlatformSupport() -> PtyPlatformSupport;
[[nodiscard]] auto CreatePlatformPtyProcess() -> std::unique_ptr<IPtyProcess>;

}  // namespace vibe::session

#endif
