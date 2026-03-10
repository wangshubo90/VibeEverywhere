#ifndef VIBE_SESSION_POSIX_PTY_PROCESS_H
#define VIBE_SESSION_POSIX_PTY_PROCESS_H

#include "vibe/session/pty_process.h"

namespace vibe::session {

class PosixPtyProcess final : public IPtyProcess {
 public:
  PosixPtyProcess() = default;
  ~PosixPtyProcess() override;

  [[nodiscard]] auto Start(const LaunchSpec& launch_spec) -> StartResult override;
  [[nodiscard]] auto Write(std::string_view input) -> bool override;
  [[nodiscard]] auto Read(int timeout_ms) -> ReadResult override;
  [[nodiscard]] auto Resize(TerminalSize terminal_size) -> bool override;
  [[nodiscard]] auto PollExit() -> std::optional<int> override;
  [[nodiscard]] auto Terminate() -> bool override;

 private:
  void CloseMasterFd();
  void ResetProcessState();

  int master_fd_{-1};
  ProcessId pid_{0};
};

}  // namespace vibe::session

#endif
