#ifndef VIBE_SESSION_PTY_PROCESS_H
#define VIBE_SESSION_PTY_PROCESS_H

#include <cstdint>
#include <string>
#include <string_view>

#include "vibe/session/launch_spec.h"

namespace vibe::session {

using ProcessId = std::int64_t;

struct StartResult {
  bool started{false};
  ProcessId pid{0};
  std::string error_message;
};

class IPtyProcess {
 public:
  virtual ~IPtyProcess() = default;

  [[nodiscard]] virtual auto Start(const LaunchSpec& launch_spec) -> StartResult = 0;
  [[nodiscard]] virtual auto Write(std::string_view input) -> bool = 0;
  [[nodiscard]] virtual auto Resize(TerminalSize terminal_size) -> bool = 0;
  [[nodiscard]] virtual auto Terminate() -> bool = 0;
};

}  // namespace vibe::session

#endif
