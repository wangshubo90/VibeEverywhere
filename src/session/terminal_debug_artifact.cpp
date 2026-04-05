#include "vibe/session/terminal_debug_artifact.h"

#include <boost/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

namespace vibe::session {
namespace {

namespace json = boost::json;

auto SanitizeFileComponent(std::string value) -> std::string {
  for (char& ch : value) {
    const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                    ch == '-' || ch == '_' || ch == '.';
    if (!ok) {
      ch = '_';
    }
  }
  return value;
}

}  // namespace

auto ToDebugJson(const TerminalScreenSnapshot& snapshot) -> std::string {
  json::object object;
  object["ptyCols"] = snapshot.columns;
  object["ptyRows"] = snapshot.rows;
  object["renderRevision"] = snapshot.render_revision;
  object["cursorRow"] = snapshot.cursor_row;
  object["cursorColumn"] = snapshot.cursor_column;
  object["visibleLines"] = json::value_from(snapshot.visible_lines);
  object["scrollbackLines"] = json::value_from(snapshot.scrollback_lines);
  object["bootstrapAnsi"] = snapshot.bootstrap_ansi;
  return json::serialize(object);
}

auto ToDebugJson(const TerminalViewportSnapshot& snapshot) -> std::string {
  json::object object;
  object["viewId"] = snapshot.view_id;
  object["cols"] = snapshot.columns;
  object["rows"] = snapshot.rows;
  object["renderRevision"] = snapshot.render_revision;
  object["totalLineCount"] = snapshot.total_line_count;
  object["viewportTopLine"] = snapshot.viewport_top_line;
  object["horizontalOffset"] = snapshot.horizontal_offset;
  if (snapshot.cursor_viewport_row.has_value()) {
    object["cursorRow"] = *snapshot.cursor_viewport_row;
  }
  if (snapshot.cursor_viewport_column.has_value()) {
    object["cursorColumn"] = *snapshot.cursor_viewport_column;
  }
  object["visibleLines"] = json::value_from(snapshot.visible_lines);
  object["bootstrapAnsi"] = snapshot.bootstrap_ansi;
  return json::serialize(object);
}

TerminalDebugRecorder::TerminalDebugRecorder(const std::string_view session_id)
    : session_id_(SanitizeFileComponent(std::string(session_id))) {
  const char* configured_root = std::getenv("VIBE_TERMINAL_TRACE_DIR");
  if (configured_root == nullptr || *configured_root == '\0') {
    return;
  }

  std::filesystem::path root(configured_root);
  root /= session_id_;
  std::error_code error;
  std::filesystem::create_directories(root, error);
  if (error) {
    return;
  }

  root_dir_ = root.string();
}

auto TerminalDebugRecorder::enabled() const -> bool { return root_dir_.has_value(); }

void TerminalDebugRecorder::RecordPtyOutput(const std::string_view raw_chunk, const std::uint64_t sequence,
                                           const TerminalScreenSnapshot& screen_snapshot) {
  if (!enabled()) {
    return;
  }

  const std::string prefix = SanitizeFileComponent(std::to_string(sequence));
  WriteFile(prefix + "-raw.bin", raw_chunk);
  WriteFile(prefix + "-screen.json", ToDebugJson(screen_snapshot));
}

void TerminalDebugRecorder::RecordViewport(const std::string_view view_id,
                                           const TerminalViewportSnapshot& viewport_snapshot) {
  if (!enabled()) {
    return;
  }

  const std::string prefix =
      SanitizeFileComponent(std::string(view_id)) + "-" + SanitizeFileComponent(std::to_string(viewport_snapshot.render_revision));
  WriteFile(prefix + "-viewport.json", ToDebugJson(viewport_snapshot));
}

void TerminalDebugRecorder::WriteFile(const std::string_view file_name, const std::string_view content) const {
  if (!root_dir_.has_value()) {
    return;
  }

  std::ofstream output(std::filesystem::path(*root_dir_) / std::string(file_name), std::ios::binary);
  output.write(content.data(), static_cast<std::streamsize>(content.size()));
}

}  // namespace vibe::session
