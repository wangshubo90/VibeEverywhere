#ifndef VIBE_SERVICE_WORKSPACE_FILE_WATCHER_H
#define VIBE_SERVICE_WORKSPACE_FILE_WATCHER_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vibe::service {

class WorkspaceFileWatcher {
 public:
  struct ObservedFile {
    std::string workspace_path;
    std::uint64_t write_time_ticks{0};
  };

  explicit WorkspaceFileWatcher(std::string workspace_root,
                                std::size_t recent_limit = 32U);

  [[nodiscard]] auto PollChangedFiles() -> std::vector<std::string>;

 private:
  [[nodiscard]] auto ScanFiles() const -> std::vector<ObservedFile>;

  std::string workspace_root_;
  std::size_t recent_limit_{32U};
  bool initialized_{false};
  std::vector<ObservedFile> known_files_;
};

}  // namespace vibe::service

#endif
