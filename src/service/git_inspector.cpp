#include "vibe/service/git_inspector.h"

#include <array>
#include <cstdio>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace vibe::service {

namespace {

auto Trim(const std::string& input) -> std::string {
  const auto start = input.find_first_not_of(" \t\n\r");
  if (start == std::string::npos) {
    return "";
  }
  const auto end = input.find_last_not_of(" \t\n\r");
  return input.substr(start, end - start + 1);
}

auto ShellEscapeSingleQuoted(const std::string& input) -> std::string {
  std::string escaped = "'";
  for (const char ch : input) {
    if (ch == '\'') {
      escaped += "'\\''";
      continue;
    }

    escaped.push_back(ch);
  }
  escaped += "'";
  return escaped;
}

}  // namespace

GitInspector::GitInspector(std::filesystem::path workspace_root)
    : workspace_root_(std::move(workspace_root)) {}

auto GitInspector::Inspect() const -> vibe::session::GitSummary {
  vibe::session::GitSummary summary;

  summary.branch = Trim(RunCommand("branch --show-current"));

  const std::string status_output = RunCommand("status --porcelain");
  std::istringstream stream(status_output);
  std::string line;

  while (std::getline(stream, line)) {
    if (line.size() < 4) {
      continue;
    }

    const char x = line[0];
    const char y = line[1];
    const std::string file = line.substr(3);

    if (x == '!' && y == '!') {
      continue;
    }

    // Staged: X is not ' ', and not '?'
    if (x != ' ' && x != '?') {
      summary.staged_files.push_back(file);
    }

    // Unstaged working tree changes include modifications, deletions, renames, and conflicts.
    if (y != ' ' && y != '?') {
      summary.modified_files.push_back(file);
    }

    // Untracked: porcelain encodes this as "??".
    if (x == '?' && y == '?') {
      summary.untracked_files.push_back(file);
    }
  }

  summary.staged_count = summary.staged_files.size();
  summary.modified_count = summary.modified_files.size();
  summary.untracked_count = summary.untracked_files.size();

  return summary;
}

auto GitInspector::RunCommand(const std::string& command) const -> std::string {
  const std::string full_command =
      "git -C " + ShellEscapeSingleQuoted(workspace_root_.string()) + " " + command + " 2>/dev/null";
  std::array<char, 128> buffer{};
  std::string result;
  std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(full_command.c_str(), "r"), pclose);

  if (!pipe) {
    return "";
  }

  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
    result += buffer.data();
  }

  return result;
}

}  // namespace vibe::service
