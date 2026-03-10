#ifndef VIBE_SERVICE_GIT_INSPECTOR_H
#define VIBE_SERVICE_GIT_INSPECTOR_H

#include <filesystem>
#include <string>

#include "vibe/session/session_snapshot.h"

namespace vibe::service {

class GitInspector {
 public:
  explicit GitInspector(std::filesystem::path workspace_root);

  [[nodiscard]] auto Inspect() const -> vibe::session::GitSummary;

 private:
  [[nodiscard]] auto RunCommand(const std::string& command) const -> std::string;

  std::filesystem::path workspace_root_;
};

}  // namespace vibe::service

#endif
