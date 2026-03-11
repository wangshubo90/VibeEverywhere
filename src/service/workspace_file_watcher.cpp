#include "vibe/service/workspace_file_watcher.h"

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <utility>

namespace vibe::service {

namespace {

auto FileTimeToTicks(const std::filesystem::file_time_type value) -> std::uint64_t {
  return static_cast<std::uint64_t>(value.time_since_epoch().count());
}

auto CompareObservedFilePath(const WorkspaceFileWatcher::ObservedFile& left,
                             const WorkspaceFileWatcher::ObservedFile& right) -> bool {
  return left.workspace_path < right.workspace_path;
}

}  // namespace

WorkspaceFileWatcher::WorkspaceFileWatcher(std::string workspace_root, const std::size_t recent_limit)
    : workspace_root_(std::move(workspace_root)),
      recent_limit_(recent_limit) {}

auto WorkspaceFileWatcher::PollChangedFiles() -> std::vector<std::string> {
  const std::vector<ObservedFile> scanned_files = ScanFiles();
  if (!initialized_) {
    known_files_ = scanned_files;
    initialized_ = true;
    return {};
  }

  std::vector<std::string> changed_files;
  for (const ObservedFile& current : scanned_files) {
    const auto previous = std::lower_bound(
        known_files_.begin(), known_files_.end(), current, CompareObservedFilePath);
    if (previous == known_files_.end() || previous->workspace_path != current.workspace_path ||
        previous->write_time_ticks != current.write_time_ticks) {
      changed_files.push_back(current.workspace_path);
    }
  }

  known_files_ = scanned_files;
  if (changed_files.size() > recent_limit_) {
    changed_files.erase(changed_files.begin(),
                        changed_files.begin() + static_cast<std::ptrdiff_t>(changed_files.size() - recent_limit_));
  }
  return changed_files;
}

auto WorkspaceFileWatcher::ScanFiles() const -> std::vector<ObservedFile> {
  std::vector<ObservedFile> files;

  std::error_code error_code;
  const std::filesystem::path root =
      std::filesystem::weakly_canonical(std::filesystem::absolute(workspace_root_), error_code);
  if (error_code || root.empty() || !std::filesystem::exists(root) || !std::filesystem::is_directory(root)) {
    return files;
  }

  std::filesystem::recursive_directory_iterator iterator(
      root, std::filesystem::directory_options::skip_permission_denied, error_code);
  const std::filesystem::recursive_directory_iterator end;
  while (!error_code && iterator != end) {
    const auto& entry = *iterator;
    const std::filesystem::path path = entry.path();
    if (entry.is_directory(error_code)) {
      if (path.filename() == ".git") {
        iterator.disable_recursion_pending();
      }
      iterator.increment(error_code);
      continue;
    }

    if (!entry.is_regular_file(error_code)) {
      iterator.increment(error_code);
      continue;
    }

    const std::filesystem::path relative = std::filesystem::relative(path, root, error_code);
    if (error_code) {
      error_code.clear();
      iterator.increment(error_code);
      continue;
    }

    const auto write_time = entry.last_write_time(error_code);
    if (error_code) {
      error_code.clear();
      iterator.increment(error_code);
      continue;
    }

    files.push_back(ObservedFile{
        .workspace_path = relative.generic_string(),
        .write_time_ticks = FileTimeToTicks(write_time),
    });

    iterator.increment(error_code);
  }

  std::sort(files.begin(), files.end(), CompareObservedFilePath);
  return files;
}

}  // namespace vibe::service
