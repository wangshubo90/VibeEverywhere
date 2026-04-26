#ifndef VIBE_SERVICE_MANAGED_LOG_PROCESS_H
#define VIBE_SERVICE_MANAGED_LOG_PROCESS_H

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <thread>

#include "vibe/service/evidence.h"
#include "vibe/session/launch_spec.h"

namespace vibe::service {

struct ManagedLogProcessStartResult {
  bool started{false};
  std::int64_t pid{0};
  std::string error_message;
};

class ManagedLogProcess {
 public:
  using OutputCallback =
      std::function<void(LogStream stream, std::string data, std::int64_t timestamp_unix_ms)>;

  ManagedLogProcess() = default;
  ManagedLogProcess(const ManagedLogProcess&) = delete;
  auto operator=(const ManagedLogProcess&) -> ManagedLogProcess& = delete;
  ManagedLogProcess(ManagedLogProcess&&) = delete;
  auto operator=(ManagedLogProcess&&) -> ManagedLogProcess& = delete;
  ~ManagedLogProcess();

  [[nodiscard]] auto Start(const vibe::session::LaunchSpec& launch_spec,
                           OutputCallback output_callback) -> ManagedLogProcessStartResult;
  [[nodiscard]] auto PollExit() -> std::optional<int>;
  [[nodiscard]] auto Terminate() -> bool;
  [[nodiscard]] auto pid() const -> std::int64_t;

 private:
  void JoinReaders();
  void StartReader(int fd, LogStream stream);

  std::int64_t pid_{0};
  OutputCallback output_callback_;
  std::thread stdout_reader_;
  std::thread stderr_reader_;
};

}  // namespace vibe::service

#endif
