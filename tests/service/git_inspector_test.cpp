#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "vibe/service/git_inspector.h"

namespace vibe::service {
namespace {

class GitInspectorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = std::filesystem::temp_directory_path() /
                ("vibe git test " +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(test_dir_);
    std::filesystem::create_directories(test_dir_);

    RunInTestDir("git init");
    RunInTestDir("git config user.email \"test@example.com\"");
    RunInTestDir("git config user.name \"Test User\"");
  }

  void TearDown() override {
    std::filesystem::remove_all(test_dir_);
  }

  void RunInTestDir(const std::string& command) {
    const std::string full_command =
        "git -C '" + test_dir_.string() + "' " + command.substr(4);
    const int result = std::system(full_command.c_str());
    ASSERT_EQ(result, 0);
  }

  void CreateFile(const std::string& name, const std::string& content = "") {
    std::ofstream file(test_dir_ / name);
    file << content;
  }

  std::filesystem::path test_dir_;
};

TEST_F(GitInspectorTest, DetectsBranchAndFileStates) {
  GitInspector inspector(test_dir_);

  // Initial state before the first commit may report no current branch name.
  vibe::session::GitSummary summary = inspector.Inspect();
  EXPECT_TRUE(summary.modified_files.empty());
  EXPECT_TRUE(summary.staged_files.empty());
  EXPECT_TRUE(summary.untracked_files.empty());

  // Untracked file
  CreateFile("untracked.txt");
  summary = inspector.Inspect();
  ASSERT_EQ(summary.untracked_files.size(), 1U);
  EXPECT_EQ(summary.untracked_files[0], "untracked.txt");

  // Staged file
  RunInTestDir("git add untracked.txt");
  summary = inspector.Inspect();
  ASSERT_EQ(summary.staged_files.size(), 1U);
  EXPECT_EQ(summary.staged_files[0], "untracked.txt");
  EXPECT_TRUE(summary.untracked_files.empty());

  // Committed file
  RunInTestDir("git commit -m \"initial commit\"");
  summary = inspector.Inspect();
  EXPECT_FALSE(summary.branch.empty());
  EXPECT_EQ(summary.staged_count, 0U);
  EXPECT_EQ(summary.modified_count, 0U);
  EXPECT_EQ(summary.untracked_count, 0U);
  EXPECT_TRUE(summary.staged_files.empty());
  EXPECT_TRUE(summary.modified_files.empty());

  // Modified file
  CreateFile("untracked.txt", "modified content");
  summary = inspector.Inspect();
  EXPECT_EQ(summary.modified_count, 1U);
  ASSERT_EQ(summary.modified_files.size(), 1U);
  EXPECT_EQ(summary.modified_files[0], "untracked.txt");
}

TEST_F(GitInspectorTest, CountsRenamedDeletedAndUntrackedChanges) {
  CreateFile("tracked.txt", "tracked");
  RunInTestDir("git add tracked.txt");
  RunInTestDir("git commit -m \"initial commit\"");

  std::filesystem::rename(test_dir_ / "tracked.txt", test_dir_ / "renamed.txt");
  CreateFile("extra.txt", "extra");

  GitInspector inspector(test_dir_);
  const vibe::session::GitSummary summary = inspector.Inspect();

  EXPECT_FALSE(summary.branch.empty());
  EXPECT_EQ(summary.staged_count, 0U);
  EXPECT_EQ(summary.modified_count, 1U);
  EXPECT_EQ(summary.untracked_count, 2U);
  EXPECT_EQ(summary.modified_files, (std::vector<std::string>{"tracked.txt"}));
  EXPECT_EQ(summary.untracked_files, (std::vector<std::string>{"extra.txt", "renamed.txt"}));
}

}  // namespace
}  // namespace vibe::service
