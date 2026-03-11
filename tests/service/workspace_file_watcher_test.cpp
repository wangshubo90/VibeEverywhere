#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "vibe/service/workspace_file_watcher.h"

namespace vibe::service {
namespace {

TEST(WorkspaceFileWatcherTest, ReportsChangedFilesAfterInitialBaseline) {
  const auto temp_root =
      std::filesystem::temp_directory_path() /
      ("vibe-workspace-file-watcher-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(temp_root / "src");

  {
    std::ofstream file(temp_root / "src" / "main.cpp");
    file << "int main() { return 0; }\n";
  }

  WorkspaceFileWatcher watcher(temp_root.string());
  EXPECT_TRUE(watcher.PollChangedFiles().empty());

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  {
    std::ofstream file(temp_root / "src" / "main.cpp");
    file << "int main() { return 1; }\n";
  }
  {
    std::ofstream file(temp_root / "README.md");
    file << "# hello\n";
  }

  const auto changed_files = watcher.PollChangedFiles();
  EXPECT_EQ(changed_files, (std::vector<std::string>{"README.md", "src/main.cpp"}));

  std::filesystem::remove_all(temp_root);
}

}  // namespace
}  // namespace vibe::service
